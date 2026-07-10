/**
 * @file    lcd.h
 * @brief   SPI LCD 屏幕 (LVGL) - 占位
 */

#ifndef MOD_LCD_H_
#define MOD_LCD_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void lcd_init(void);
void lcd_task(void *pvParameters);

#endif /* MOD_LCD_H_ */
