/**
 * @file    esp32.h
 * @brief   ESP32 SPI 从机通信模块
 * @note    SPI2 从机模式 (1MHz MODE0)
 *          传感器帧 (14 字节): [0xAA][0x55][0x01][temp:2][humi:2][co2:2][nh3:2][lux:2][crc8]
 *          OTA 帧 (可变长度, 44~522字节): [0xAA][0x55][type][payload:N][crc8]
 */

#ifndef MOD_ESP32_H_
#define MOD_ESP32_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "bsp/bsp_config.h"

/* ========================== 帧协议 ========================== */
#define ESP32_FRAME_HEADER0   0xAA
#define ESP32_FRAME_HEADER1   0x55

/* 帧类型 */
#define ESP32_FRAME_SENSOR    0x01   /**< 传感器数据帧 (14字节) */
#define ESP32_FRAME_OTA_HANDSHAKE 0xF0 /**< OTA握手帧 (44字节) */
#define ESP32_FRAME_OTA_DATA  0xF1   /**< OTA数据帧 (10+N字节) */
#define ESP32_FRAME_OTA_RESULT 0xF2  /**< OTA结果帧 (6字节) */

/* OTA 帧大小限制 */
#define ESP32_OTA_FRAME_MAX   522    /**< OTA最大帧: 2hdr+1type+4off+2len+512data+1crc */

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

/* ========================== OTA 回调 ========================== */
/** OTA 帧接收回调: frame_type, frame_data, frame_len */
typedef void (*esp32_ota_frame_cb_t)(uint8_t type, const uint8_t *data, uint16_t len);
extern esp32_ota_frame_cb_t g_esp32_ota_frame_cb;

/* ========================== OTA 触发 ========================== */
/** STM32→ESP32 MISO 命令码 */
#define ESP32_CMD_IDLE        0x00   /**< 无命令（正常传感器模式） */
#define ESP32_CMD_OTA_START   0x10   /**< 请求 ESP32 开始 OTA 流程 */
#define ESP32_CMD_OTA_CANCEL  0x20   /**< 取消 OTA */

/** 设置 MISO 响应字节（ESP32 下次 SPI 传输时收到） */
void esp32_set_miso_cmd(uint8_t cmd);

/* ========================== API ========================== */
void     esp32_init(void);
void     esp32_task(void *pvParameters);
uint16_t esp32_get_status(void);
void     esp32_get_data(Esp32SensorData *out);

#endif /* MOD_ESP32_H_ */
