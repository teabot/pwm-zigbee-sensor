#pragma once

#include <stdint.h>

#define ZB_ENDPOINT_CH0  1
#define ZB_ENDPOINT_CH1  2

/**
 * Initialise and start the Zigbee stack.
 * Blocks until the device joins a network (or times out and retries).
 * Spawns internal tasks; do not call more than once.
 */
void zigbee_init(void);

/**
 * Update the Analog Input present_value on the given endpoint.
 * Safe to call from any task after zigbee_init() returns.
 *
 * @param endpoint  ZB_ENDPOINT_CH0 or ZB_ENDPOINT_CH1
 * @param duty      Duty cycle in percent (0.0 – 100.0). Pass -1.0 when invalid.
 */
void zigbee_set_duty(uint8_t endpoint, float duty);
