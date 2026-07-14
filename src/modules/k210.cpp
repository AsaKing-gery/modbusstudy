/**
 * @file    k210.cpp
 * @brief   K210 摄像头模块 - 占位实现
 */

#include "k210.h"
#include "bsp/bsp_debug.h"
#include <STM32FreeRTOS.h>

void k210_init(void)
{
    DBG("K210", "init skipped");
}

void k210_task(void *pvParameters)
{
    (void)pvParameters;
    DBG("K210", "task idle");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
