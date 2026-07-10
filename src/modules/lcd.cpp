/**
 * @file    lcd.cpp
 * @brief   SPI LCD 屏幕 (LVGL) - 占位实现
 */

#include "lcd.h"
#include "app/app_debug.h"
#include <STM32FreeRTOS.h>

void lcd_init(void)
{
    DBG("LCD", "init skipped");
}

void lcd_task(void *pvParameters)
{
    (void)pvParameters;
    DBG("LCD", "task idle");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
