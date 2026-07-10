/**
 * @file    esp32.cpp
 * @brief   ESP32 SPI 通信模块 - 占位实现
 */

#include "esp32.h"
#include "app/app_debug.h"
#include <STM32FreeRTOS.h>

void esp32_init(void)
{
    DBG("ESP32", "init skipped (not configured)");
}

void esp32_task(void *pvParameters)
{
    (void)pvParameters;
    DBG("ESP32", "task idle");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
