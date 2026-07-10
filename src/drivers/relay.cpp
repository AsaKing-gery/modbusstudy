/**
 * @file    relay.cpp
 * @brief   8路继电器驱动实现
 */

#include "relay.h"

/** 继电器引脚映射表 */
static const uint32_t relay_pin_map[RELAY_CHANNEL_COUNT] = {
    PIN_HUMIDIFIER_1, PIN_HUMIDIFIER_2, PIN_HUMIDIFIER_3, PIN_HUMIDIFIER_4,
    PIN_FAN_1,        PIN_FAN_2,        PIN_FAN_3,        PIN_FAN_4
};

/** 当前状态缓存 */
static uint8_t relay_state = 0x00;

/**
 * @brief 初始化继电器（已在 bsp_gpio_init 中设置过引脚模式，此处仅初始化缓存）
 */
void relay_init(void)
{
    relay_state = 0x00;
}

void relay_set(uint8_t channel, bool on)
{
    if (channel >= RELAY_CHANNEL_COUNT) return;

    if (on) {
        relay_state |= (1 << channel);
    } else {
        relay_state &= ~(1 << channel);
    }
    digitalWrite(relay_pin_map[channel], on ? RELAY_ACTIVE : !RELAY_ACTIVE);
}

void relay_set_all(uint8_t mask)
{
    relay_state = mask;
    for (uint8_t i = 0; i < RELAY_CHANNEL_COUNT; i++) {
        digitalWrite(relay_pin_map[i], (mask & (1 << i)) ? RELAY_ACTIVE : !RELAY_ACTIVE);
    }
}

bool relay_get_state(uint8_t channel)
{
    if (channel >= RELAY_CHANNEL_COUNT) return false;
    return (relay_state >> channel) & 0x01;
}

uint8_t relay_get_all(void)
{
    return relay_state;
}
