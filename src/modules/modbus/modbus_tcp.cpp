/**
 * @file    modbus_tcp.cpp
 * @brief   Modbus TCP 服务器 - 占位实现
 * @note    TODO: 通过 ESP32 SPI 桥接提供 WiFi → Modbus TCP 透传
 */

#include "modbus_tcp.h"
#include "bsp/bsp_debug.h"
#include <STM32FreeRTOS.h>

void modbus_tcp_init(void)
{
    DBG("MODTCP", "init skipped (ESP32 bridge not ready)");
}

void modbus_tcp_task(void *pvParameters)
{
    (void)pvParameters;
    DBG("MODTCP", "task started (idle)");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
