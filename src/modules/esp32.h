/**
 * @file    esp32.h
 * @brief   ESP32 SPI 从机通信模块 — 工业级握手协议
 * @note    SPI2 从机模式 (1MHz MODE0)
 *          传感器帧 (14 字节): [0xAA][0x55][0x01][temp:2][humi:2][co2:2][nh3:2][lux:2][crc8]
 *          OTA 帧 (可变长度, 44~522字节): [0xAA][0x55][type][payload:N][crc8]
 *
 * 握手协议:
 *   ESP32 发起 HANDSHAKE_REQ → STM32 在 MISO 上输出 9B HS_RESPONSE
 *   → ESP32 发送 HANDSHAKE_ACK → 握手完成 → 进入正常数据交换
 *   空闲期每 10s 发送 HEARTBEAT 保持连接
 */

#ifndef MOD_ESP32_H_
#define MOD_ESP32_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "bsp/bsp_config.h"

/* ========================== 帧协议 ========================== */
#define ESP32_FRAME_HEADER0   0xAA
#define ESP32_FRAME_HEADER1   0x55

/* MISO 响应帧头（STM32→ESP32 握手响应，区别于 MOSI 帧头 0xAA 0x55） */
#define ESP32_MISO_HS_RESP_HDR0  0xBB
#define ESP32_MISO_HS_RESP_HDR1  0x66

/* 帧类型 */
#define ESP32_FRAME_SENSOR          0x01 /**< 传感器数据帧 (14字节) */
#define ESP32_FRAME_BOOT_REPORT     0x02 /**< (保留) STM32→ESP32 启动版本报告 */
#define ESP32_FRAME_OTA_HANDSHAKE   0xF0 /**< OTA握手帧 (522字节填充) */
#define ESP32_FRAME_OTA_DATA        0xF1 /**< OTA数据帧 (10+N字节) */
#define ESP32_FRAME_OTA_RESULT      0xF2 /**< OTA结果帧 (6字节) */

/* 握手协议帧 */
#define ESP32_FRAME_HANDSHAKE_REQ   0xFD /**< 握手请求: [AA][55][FD][seq][crc8] 5B */
#define ESP32_FRAME_HANDSHAKE_ACK   0xFE /**< 握手确认: [AA][55][FE][seq][crc8] 5B */
#define ESP32_FRAME_HEARTBEAT       0xFC /**< 心跳保活: [AA][55][FC][seq][crc8] 5B */

/* 握手/心跳帧长度 */
#define ESP32_HANDSHAKE_FRAME_LEN   5    /**< 握手/心跳帧: 2hdr+1type+1seq+1crc */
#define ESP32_HS_RESP_LEN           9    /**< MISO握手响应: [BB][66][state][ver:4BE][boot][crc] */

/* OTA 帧大小限制 */
#define ESP32_OTA_FRAME_MAX         700  /**< OTA最大帧 */

/* ========================== 握手协议 ========================== */

/** STM32 握手状态 */
typedef enum {
    HS_INIT      = 0,  /**< 初始状态，等待 ESP32 发起握手 */
    HS_RESPONSE  = 1,  /**< 已收到 HANDSHAKE_REQ，MISO 输出 9B 响应帧循环 */
    HS_READY     = 2   /**< 握手完成，进入正常数据交换 */
} Esp32HsState_t;

/** 握手超时常量 (ms) */
#define HS_ACK_TIMEOUT_MS       10000 /**< 等待 HANDSHAKE_ACK 超时 (10s) */
#define HS_HEARTBEAT_TIMEOUT_MS 30000 /**< 心跳超时 → ESP32 断连 (30s) */
#define HS_RETRY_MAX            3     /**< 握手最大重试次数 */

/* ========================== 握手 API ========================== */

/** 获取握手状态 */
Esp32HsState_t esp32_get_hs_state(void);

/** 握手是否已完成 (HS_READY) */
bool esp32_hs_ready(void);

/** 上报心跳收到（由帧处理回调调用） */
void esp32_hs_heartbeat_rx(void);

/** 获取 boot_status 用于握手响应 */
uint8_t esp32_hs_get_boot_status(void);

/** 设置 boot_status (OTA 模块调用) */
void esp32_hs_set_boot_status(uint8_t status);

/* ========================== BOOT_REPORT (保留兼容，内部使用) ========================== */

/** BOOT_REPORT boot_status */
#define BOOT_STATUS_NORMAL      0x00  /**< 正常启动 (boot_success已确认) */
#define BOOT_STATUS_FIRST_BOOT  0x01  /**< OTA后首次启动 (未确认) */
#define BOOT_STATUS_ROLLBACK    0xFF  /**< 回滚启动 (上次OTA失败) */

void boot_report_start(uint32_t version, uint8_t boot_status);
void boot_report_stop(void);

/* ========================== 传感器数据结构体 ========================== */
struct Esp32SensorData
{
    int16_t temp_x100;
    int16_t humi_x100;
    int16_t co2;
    int16_t nh3_x100;
    int16_t lux_x100;
    bool    valid;
    uint32_t update_ms;
};

/* ========================== OTA 回调 ========================== */
typedef void (*esp32_ota_frame_cb_t)(uint8_t type, const uint8_t *data, uint16_t len);
extern esp32_ota_frame_cb_t g_esp32_ota_frame_cb;

/* ========================== OTA 触发 ========================== */
#define ESP32_CMD_IDLE            0x00
#define ESP32_CMD_OTA_START       0x10
#define ESP32_CMD_OTA_CANCEL      0x20
#define ESP32_CMD_OTA_CHUNK_ACK   0xA5  /* OTA 块写入完成 ACK (ESP32 等待此信号) */

void esp32_set_miso_cmd(uint8_t cmd);
void esp32_clear_miso_cmd(void);

/* ========================== 基础 API ========================== */
void     esp32_init(void);
void     esp32_task(void *pvParameters);
uint16_t esp32_get_status(void);
void     esp32_get_data(Esp32SensorData *out);

#endif /* MOD_ESP32_H_ */
