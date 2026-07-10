/**
 * @file    esp32.h
 * @brief   ESP32 SPI 通信模块 - 占位
 * @note    SPI2 连接 ESP32 作为从机，提供 WiFi + TCP 桥接
 */

#ifndef MOD_ESP32_H_
#define MOD_ESP32_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void esp32_init(void);
void esp32_task(void *pvParameters);

#endif /* MOD_ESP32_H_ */
