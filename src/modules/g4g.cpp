/**
 * @file    g4g.cpp
 * @brief   4G 模块 - 占位实现
 */

#include "g4g.h"
#include "bsp/bsp_debug.h"
#include <STM32FreeRTOS.h>

void g4g_init(void)
{
    DBG("4G", "init skipped");
}

void g4g_task(void *pvParameters)
{
    (void)pvParameters;
    DBG("4G", "task idle");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
