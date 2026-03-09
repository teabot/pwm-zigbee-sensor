#pragma once

#include <stdbool.h>
#include <stdint.h>

// GPIO pins for the two PWM inputs — change to suit your wiring
#define PWM_CAPTURE_GPIO_CH0  4
#define PWM_CAPTURE_GPIO_CH1  5

// Number of channels
#define PWM_CAPTURE_NUM_CHANNELS 2

// Capture timer resolution: 1 MHz → 1 µs per tick
#define PWM_CAPTURE_RESOLUTION_HZ 1000000

// If no edge is seen within this many ms, report duty cycle as 0
#define PWM_CAPTURE_TIMEOUT_MS 500

typedef struct {
    float duty_percent;  // 0.0 – 100.0; negative means no valid signal
    bool  valid;
} pwm_measurement_t;

/**
 * Initialise MCPWM capture for both channels.
 * Must be called before pwm_capture_get().
 */
void pwm_capture_init(void);

/**
 * Return the latest duty-cycle measurement for a channel (0 or 1).
 * Thread-safe; can be called from any task.
 */
pwm_measurement_t pwm_capture_get(int channel);
