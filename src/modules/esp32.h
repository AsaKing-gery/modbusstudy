/**
 * @file    esp32.h
 * @brief   ESP32 SPI 从机通信模块
 * @note    SPI2 从机模式 (1MHz MODE0)，接收 14 字节帧:
 *          [0xAA][0x55][0x01][temp_x100:2][humi_x100:2][co2:2][nh3_x100:2][lux_x100:2][crc8]
 *          所有数据 int16 大端序，定点数 ×100
 */

#ifndef MOD_ESP32_H_
#define MOD_ESP32_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "bsp/bsp_config.h"

/* ========================== 帧协议 ========================== */
#define ESP32_FRAME_HEADER0   0xAA
#define ESP32_FRAME_HEADER1   0x55
#define ESP32_FRAME_TYPE      0x01

/* ========================== 传感器数据结构体 ========================== */
struct Esp32SensorData
{
    int16_t temp_x100;       /**< 温度 ×100 °C */
    int16_t humi_x100;       /**< 湿度 ×100 % */
    int16_t co2;             /**< CO2 ppm */
    int16_t nh3_x100;        /**< NH3 ×100 ppm */
    int16_t lux_x100;        /**< 光照 ×100 lux */
    bool    valid;           /**< 当前帧校验通过 */
    uint32_t update_ms;      /**< 最近更新时间 ms */
};

/* ========================== API ========================== */
void     esp32_init(void);
void     esp32_task(void *pvParameters);
uint16_t esp32_get_status(void);
void     esp32_get_data(Esp32SensorData *out);

#endif /* MOD_ESP32_H_ */
