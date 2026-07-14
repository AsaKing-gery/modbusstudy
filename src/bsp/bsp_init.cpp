/**
 * @file    bsp_init.cpp
 * @brief   板级初始化 - GPIO 与外设配置
 */

#include "bsp_config.h"
#include "bsp_debug.h"

/* ========================== 调试串口实例 (USART2: PD5=TX, PD6=RX) ========================== */
HardwareSerial DebugSerial(PD6, PD5);   /**< RX, TX */

/* ========================== RS485 串口实例 (USART1: PA9=TX, PA10=RX) ========================== */
HardwareSerial RS485_SERIAL(PIN_RS485_RX, PIN_RS485_TX);

/**
 * @brief 获取 MCU 96 位唯一 ID 中的 32 位字
 * @param offset_index 偏移索引 (0~2)
 * @return 32 位 UID 字
 */
uint32_t bsp_get_mcu_id(uint8_t offset_index)
{
    if (offset_index > 2) offset_index = 2;
    return *(uint32_t *)(0x1FFF7A10 + (offset_index * 4));
}

/**
 * @brief 打印系统信息（时钟 + MCU ID）
 */
/**
 * @brief 打印系统信息（时钟 + MCU ID）
 */
void bsp_show_system_info(void)
{
    TRACE("2");

    DEBUG_SERIAL.println();
    DEBUG_SERIAL.print("SYSCLK: ");  DEBUG_SERIAL.println(SystemCoreClock);
    DEBUG_SERIAL.print("HCLK: ");    DEBUG_SERIAL.println(HAL_RCC_GetHCLKFreq());
    DEBUG_SERIAL.print("PCLK1: ");   DEBUG_SERIAL.println(HAL_RCC_GetPCLK1Freq());
    DEBUG_SERIAL.print("PCLK2: ");   DEBUG_SERIAL.println(HAL_RCC_GetPCLK2Freq());

    DEBUG_SERIAL.print("MCU ID 0:");  DEBUG_SERIAL.print(bsp_get_mcu_id(0), HEX);
    DEBUG_SERIAL.print(" 1:");        DEBUG_SERIAL.print(bsp_get_mcu_id(1), HEX);
    DEBUG_SERIAL.print(" 2:");        DEBUG_SERIAL.println(bsp_get_mcu_id(2), HEX);
    DEBUG_SERIAL.flush();
}

/**
 * @brief 初始化所有 GPIO 外设
 * @note  上电默认：继电器全部断开 (HIGH)，LED 熄灭
 */
void bsp_gpio_init(void)
{
    TRACE("3");
    TRACE("G");

    /* --- 运行指示灯 --- */
    pinMode(PIN_RUN_LED, OUTPUT);
    digitalWrite(PIN_RUN_LED, !LED_ACTIVE);
    pinMode(PIN_ERROR_LED, OUTPUT);
    digitalWrite(PIN_ERROR_LED, !LED_ACTIVE);
    TRACE("G1");

    /* --- 8路继电器，全部断开 --- */
    const uint32_t relay_pins[PIN_RELAY_COUNT] = {
        PIN_HUMIDIFIER_1, PIN_HUMIDIFIER_2, PIN_HUMIDIFIER_3, PIN_HUMIDIFIER_4,
        PIN_FAN_1,        PIN_FAN_2,        PIN_FAN_3,        PIN_FAN_4
    };
    for (int i = 0; i < PIN_RELAY_COUNT; i++) {
        pinMode(relay_pins[i], OUTPUT);
        digitalWrite(relay_pins[i], HIGH);
    }
    TRACE("G2");

    /* --- 4G 模块控制引脚 --- */
    pinMode(PIN_G4G_RDY, INPUT);
    pinMode(PIN_G4G_DTR, OUTPUT);
    digitalWrite(PIN_G4G_DTR, LOW);
    pinMode(PIN_G4G_RST, OUTPUT);
    digitalWrite(PIN_G4G_RST, HIGH);
    TRACE("G3");

    /* --- LCD 背光和 SPI 控制引脚 --- */
    pinMode(PIN_LCD_BLK, OUTPUT);
    digitalWrite(PIN_LCD_BLK, LOW);
    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);
    pinMode(PIN_LCD_DC, OUTPUT);
    digitalWrite(PIN_LCD_DC, LOW);
    TRACE("G4");

    /* --- RS485 方向控制 --- */
    pinMode(PIN_RS485_EN, OUTPUT);
    digitalWrite(PIN_RS485_EN, LOW);
    TRACE("G5");

    TRACE("G6");
    TRACE("G7");
    TRACE("g");
}
