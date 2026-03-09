#include "pwm_capture.h"

#include <string.h>
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "pwm_cap";

typedef struct {
    uint32_t           rise_tick;       // timer value at last rising edge
    uint32_t           fall_tick;       // timer value at last falling edge
    uint32_t           period_ticks;    // ticks between last two rising edges
    bool               rise_seen;       // have we seen the first rising edge?
    float              duty_percent;
    bool               valid;
    int64_t            last_edge_us;    // esp_timer timestamp of last edge (timeout detection)
    SemaphoreHandle_t  mutex;
} channel_state_t;

static channel_state_t s_ch[PWM_CAPTURE_NUM_CHANNELS];
static mcpwm_cap_channel_handle_t s_cap_chan[PWM_CAPTURE_NUM_CHANNELS];

static bool IRAM_ATTR cap_callback(mcpwm_cap_channel_handle_t chan,
                                   const mcpwm_capture_event_data_t *edata,
                                   void *user_data)
{
    int ch = (int)(intptr_t)user_data;
    channel_state_t *s = &s_ch[ch];
    BaseType_t high_task_wakeup = pdFALSE;

    if (edata->cap_edge == MCPWM_CAP_EDGE_POS) {
        // Rising edge: record period, reset high-time counter
        if (s->rise_seen) {
            s->period_ticks = edata->cap_value - s->rise_tick;
        }
        s->rise_tick  = edata->cap_value;
        s->rise_seen  = true;
    } else {
        // Falling edge: record high time
        if (s->rise_seen) {
            uint32_t high_ticks = edata->cap_value - s->rise_tick;
            s->fall_tick = edata->cap_value;

            if (s->period_ticks > 0) {
                float duty = (float)high_ticks / (float)s->period_ticks * 100.0f;
                // Clamp to 0–100 in case of jitter
                if (duty < 0.0f)   duty = 0.0f;
                if (duty > 100.0f) duty = 100.0f;

                // Write under mutex — try non-blocking from ISR
                if (xSemaphoreTakeFromISR(s->mutex, &high_task_wakeup) == pdTRUE) {
                    s->duty_percent = duty;
                    s->valid        = true;
                    s->last_edge_us = esp_timer_get_time();
                    xSemaphoreGiveFromISR(s->mutex, &high_task_wakeup);
                }
            }
        }
    }

    return high_task_wakeup == pdTRUE;
}

void pwm_capture_init(void)
{
    static const int gpio[PWM_CAPTURE_NUM_CHANNELS] = {
        PWM_CAPTURE_GPIO_CH0,
        PWM_CAPTURE_GPIO_CH1,
    };

    // Single capture timer shared by both channels
    mcpwm_cap_timer_handle_t cap_timer;
    mcpwm_capture_timer_config_t timer_cfg = {
        .clk_src       = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
        .group_id      = 0,
        .resolution_hz = PWM_CAPTURE_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(mcpwm_new_capture_timer(&timer_cfg, &cap_timer));

    for (int i = 0; i < PWM_CAPTURE_NUM_CHANNELS; i++) {
        memset(&s_ch[i], 0, sizeof(s_ch[i]));
        s_ch[i].mutex = xSemaphoreCreateMutex();
        configASSERT(s_ch[i].mutex);

        mcpwm_capture_channel_config_t ch_cfg = {
            .gpio_num   = gpio[i],
            .prescale   = 1,
            .flags = {
                .pos_edge   = true,
                .neg_edge   = true,
                .pull_up    = true,   // internal pull-up; remove if driving from low-impedance source
                .invert_cap_signal = false,
            },
        };
        ESP_ERROR_CHECK(mcpwm_new_capture_channel(cap_timer, &ch_cfg, &s_cap_chan[i]));

        mcpwm_capture_event_callbacks_t cbs = {
            .on_cap = cap_callback,
        };
        ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(
            s_cap_chan[i], &cbs, (void *)(intptr_t)i));

        ESP_ERROR_CHECK(mcpwm_capture_channel_enable(s_cap_chan[i]));

        ESP_LOGI(TAG, "Channel %d on GPIO %d", i, gpio[i]);
    }

    ESP_ERROR_CHECK(mcpwm_capture_timer_enable(cap_timer));
    ESP_ERROR_CHECK(mcpwm_capture_timer_start(cap_timer));

    ESP_LOGI(TAG, "Capture timer started @ %d Hz", PWM_CAPTURE_RESOLUTION_HZ);
}

pwm_measurement_t pwm_capture_get(int channel)
{
    pwm_measurement_t result = { .duty_percent = 0.0f, .valid = false };

    if (channel < 0 || channel >= PWM_CAPTURE_NUM_CHANNELS) {
        return result;
    }

    channel_state_t *s = &s_ch[channel];

    if (xSemaphoreTake(s->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Invalidate if no edge seen recently (signal absent / DC)
        int64_t age_ms = (esp_timer_get_time() - s->last_edge_us) / 1000;
        if (s->valid && age_ms > PWM_CAPTURE_TIMEOUT_MS) {
            s->valid        = false;
            s->duty_percent = 0.0f;
            s->rise_seen    = false;
        }
        result.duty_percent = s->duty_percent;
        result.valid        = s->valid;
        xSemaphoreGive(s->mutex);
    }

    return result;
}
