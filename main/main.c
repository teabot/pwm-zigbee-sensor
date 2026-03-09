#include "pwm_capture.h"
#include "zigbee.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

// How often to poll the PWM measurements (milliseconds)
#define POLL_INTERVAL_MS  200

// Minimum change in duty cycle (%) required to trigger a Zigbee report.
// Prevents flooding the network with noise-driven updates.
#define REPORT_THRESHOLD  0.5f

// Sentinel meaning "never reported yet" — outside 0–100 range
#define LAST_VALUE_INIT   -1.0f

static void sensor_task(void *pvParameters)
{
    float last_reported[PWM_CAPTURE_NUM_CHANNELS];
    for (int i = 0; i < PWM_CAPTURE_NUM_CHANNELS; i++) {
        last_reported[i] = LAST_VALUE_INIT;
    }

    while (true) {
        for (int ch = 0; ch < PWM_CAPTURE_NUM_CHANNELS; ch++) {
            pwm_measurement_t m = pwm_capture_get(ch);

            uint8_t endpoint = (ch == 0) ? ZB_ENDPOINT_CH0 : ZB_ENDPOINT_CH1;
            float   value    = m.valid ? m.duty_percent : 0.0f;

            float delta = value - last_reported[ch];
            if (delta < 0) delta = -delta;

            if (last_reported[ch] == LAST_VALUE_INIT || delta >= REPORT_THRESHOLD) {
                zigbee_set_duty(endpoint, value);
                last_reported[ch] = value;
                ESP_LOGI(TAG, "CH%d: %.1f%% reported (delta=%.2f%%)",
                         ch, value, delta);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    // NVS is required for Zigbee to store network keys
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    pwm_capture_init();
    zigbee_init();

    // Give the Zigbee stack a moment to start before we begin pushing attributes
    vTaskDelay(pdMS_TO_TICKS(2000));

    xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);
}
