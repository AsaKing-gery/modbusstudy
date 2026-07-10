/**
 * @file    led.h
 * @brief   运行/错误指示灯驱动
 * @note    LED 低电平点亮 (灌电流驱动)，极性由 bsp_config.h 的 LED_ACTIVE 控制
 */

#ifndef DRV_LED_H_
#define DRV_LED_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void led_init(void);
void led_run_set(bool on);
void led_run_toggle(void);
void led_error_on(void);
void led_error_off(void);

#endif /* DRV_LED_H_ */
