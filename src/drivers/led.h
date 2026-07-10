/**
 * @file    led.h
 * @brief   运行/错误指示灯驱动
 */

#ifndef DRV_LED_H_
#define DRV_LED_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void led_init(void);
void led_run_toggle(void);
void led_error_on(void);
void led_error_off(void);

#endif /* DRV_LED_H_ */
