/**
 * @file    led.cpp
 * @brief   运行/错误指示灯驱动实现
 */

#include "led.h"

void led_init(void)
{
    /* 引脚模式已在 bsp_gpio_init 中设置，初始熄灭（反电平） */
    digitalWrite(PIN_RUN_LED, !LED_ACTIVE);
    digitalWrite(PIN_ERROR_LED, !LED_ACTIVE);
}

void led_run_set(bool on)
{
    digitalWrite(PIN_RUN_LED, on ? LED_ACTIVE : !LED_ACTIVE);
}

void led_run_toggle(void)
{
    digitalWrite(PIN_RUN_LED, !digitalRead(PIN_RUN_LED));
}

void led_error_on(void)
{
    digitalWrite(PIN_ERROR_LED, LED_ACTIVE);
}

void led_error_off(void)
{
    digitalWrite(PIN_ERROR_LED, !LED_ACTIVE);
}
