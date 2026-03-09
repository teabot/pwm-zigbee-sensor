#include "zigbee.h"

#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "zigbee";

// ── Zigbee network configuration ─────────────────────────────────────────────

#define ZB_CHANNEL_MASK  ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK  // scan all channels

// End Device config — simpler interview, no routing overhead
#define ZB_ED_CONFIG()                              \
    {                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,      \
        .install_code_policy = false,               \
        .nwk_cfg.zed_cfg = {                        \
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN, \
            .keep_alive = 3000,                     \
        }                                           \
    }

// ── Helpers ───────────────────────────────────────────────────────────────────

static esp_zb_attribute_list_t *make_humidity_cluster(void)
{
    // Relative Humidity cluster (0x0405) — ZHA auto-creates sensor entities for this.
    // measured_value = duty_percent * 100  (so 50% → 5000; ZHA divides by 100 to display)
    esp_zb_humidity_meas_cluster_cfg_t cfg = {
        .measured_value = 0,
        .min_value      = 0,
        .max_value      = 10000,  // 100.00 %
    };
    return esp_zb_humidity_meas_cluster_create(&cfg);
}

static void add_endpoint(esp_zb_ep_list_t *ep_list, uint8_t ep_id)
{
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();

    // Basic cluster
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01,  // mains
    };
    // ZCL strings: first byte is length, followed by characters (no null terminator)
    static char manufacturer[] = "\x03" "DIY";
    static char model_id[]     = "\x09" "PWMSensor";

    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                  (void *)model_id);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                  (void *)manufacturer);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(
        clusters, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Identify cluster (required for ZHA)
    esp_zb_identify_cluster_cfg_t id_cfg = { .identify_time = 0 };
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
        clusters, esp_zb_identify_cluster_create(&id_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Humidity cluster — measured_value = duty_percent * 100
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_humidity_meas_cluster(
        clusters, make_humidity_cluster(), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Register endpoint
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint        = ep_id,
        .app_profile_id  = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id   = 0x0307,  // HA profile: Humidity Sensor device type
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg));
}

// ── Zigbee signal handler (mandatory — called by the stack) ──────────────────

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t          *p_sg_p = signal_struct->p_app_signal;
    esp_err_t          err    = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = *p_sg_p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialising Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Device started — starting network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Failed to start, status: %s — retrying", esp_err_to_name(err));
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err == ESP_OK) {
            esp_zb_ieee_addr_t ext_pan_id;
            esp_zb_get_extended_pan_id(ext_pan_id);
            ESP_LOGI(TAG, "Joined network. PAN ID: 0x%04hx, channel: %d",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
        } else {
            ESP_LOGW(TAG, "Network steering failed (err=0x%x: %s) — retrying",
                     err, esp_err_to_name(err));
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Left network — restarting steering");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;

    default:
        ESP_LOGD(TAG, "ZB signal: 0x%x, status: %s", sig, esp_err_to_name(err));
        break;
    }
}

// ── ZHA stack callbacks ───────────────────────────────────────────────────────

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        // We don't accept write commands on our attributes, but handle gracefully
        break;
    default:
        ESP_LOGD(TAG, "Unhandled ZB action: 0x%04x", callback_id);
        break;
    }
    return ESP_OK;
}

static void esp_zb_task(void *pvParameters)
{
    // Initialise Zigbee stack
    esp_zb_cfg_t zb_cfg = ZB_ED_CONFIG();
    esp_zb_init(&zb_cfg);

    // Register our two endpoints
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    add_endpoint(ep_list, ZB_ENDPOINT_CH0);
    add_endpoint(ep_list, ZB_ENDPOINT_CH1);
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    // Register action handler and set channel
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ZB_CHANNEL_MASK);

    ESP_LOGI(TAG, "Starting Zigbee stack — waiting for network...");
    ESP_ERROR_CHECK(esp_zb_start(false));

    // This call blocks and runs the Zigbee event loop
    esp_zb_stack_main_loop();
}

// ── Public API ────────────────────────────────────────────────────────────────

void zigbee_init(void)
{
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,       // use built-in 802.15.4 radio
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

    xTaskCreate(esp_zb_task, "zb_main", 8192, NULL, 5, NULL);
}

void zigbee_set_duty(uint8_t endpoint, float duty)
{
    // Humidity cluster stores measured_value as duty_percent * 100 (uint16)
    uint16_t val = (uint16_t)(duty * 100.0f);

    esp_zb_lock_acquire(portMAX_DELAY);
    ESP_ERROR_CHECK(esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
        &val,
        false));
    esp_zb_lock_release();

    ESP_LOGD(TAG, "ep%d duty=%.1f%%", endpoint, duty);
}
