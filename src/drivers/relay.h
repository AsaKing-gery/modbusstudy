/**
 * @file    relay.h
 * @brief   8路继电器驱动
 */

#ifndef DRV_RELAY_H_
#define DRV_RELAY_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

#define RELAY_CHANNEL_COUNT   PIN_RELAY_COUNT
#define RELAY_ACTIVE          LOW   /**< 低电平闭合 */

void relay_init(void);
void relay_set(uint8_t channel, bool on);
void relay_set_all(uint8_t mask);
bool relay_get_state(uint8_t channel);
uint8_t relay_get_all(void);

#endif /* DRV_RELAY_H_ */
