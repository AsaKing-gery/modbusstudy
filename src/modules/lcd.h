/**
 * @file    lcd.h
 * @brief   ST7789 SPI LCD 驱动 (SPI1, 240x240 RGB565)
 * @note    适配自 鹿小班 STM32F407VET6 例程, 改用 HAL/Arduino API
 */

#ifndef MOD_LCD_H_
#define MOD_LCD_H_

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include "bsp/bsp_config.h"
#include "lcd_fonts.h"
#include "lcd_image.h"

/* ========================== 屏幕参数 ========================== */
#define LCD_Width     240
#define LCD_Height    240

/* ========================== 显示方向 ========================== */
#define Direction_H         0   /* 水平显示 */
#define Direction_H_Flip    1   /* 水平, 上下翻转 */
#define Direction_V         2   /* 垂直显示 */
#define Direction_V_Flip    3   /* 垂直, 上下翻转 */

/* ========================== 数字填充模式 ========================== */
#define Fill_Zero   0   /* 不足位补0 */
#define Fill_Space  1   /* 不足位补空格 */

/* ========================== 颜色定义 (RGB888 → 驱动内转 RGB565) ========================== */
#define LCD_WHITE       0xFFFFFF
#define LCD_BLACK       0x000000
#define LCD_BLUE        0x0000FF
#define LCD_GREEN       0x00FF00
#define LCD_RED         0xFF0000
#define LCD_CYAN        0x00FFFF
#define LCD_MAGENTA     0xFF00FF
#define LCD_YELLOW      0xFFFF00
#define LCD_GREY        0x2C2C2C

#define LIGHT_BLUE      0x8080FF
#define LIGHT_GREEN     0x80FF80
#define LIGHT_RED       0xFF8080
#define LIGHT_CYAN      0x80FFFF
#define LIGHT_MAGENTA   0xFF80FF
#define LIGHT_YELLOW    0xFFFF80
#define LIGHT_GREY      0xA3A3A3

#define DARK_BLUE       0x000080
#define DARK_GREEN      0x008000
#define DARK_RED        0x800000
#define DARK_CYAN       0x008080
#define DARK_MAGENTA    0x800080
#define DARK_YELLOW     0x808000
#define DARK_GREY       0x404040

/* ========================== API ========================== */

/* 初始化 */
void SPI_LCD_Init(void);
void lcd_init(void);
void lcd_task(void *pvParameters);

/* 清屏 */
void LCD_Clear(void);
void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

/* 颜色与方向 */
void LCD_SetColor(uint32_t Color);
void LCD_SetBackColor(uint32_t Color);
void LCD_SetDirection(uint8_t direction);

/* ASCII 字符 */
void LCD_SetAsciiFont(pFONT *fonts);
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c);
void LCD_DisplayString(uint16_t x, uint16_t y, const char *p);

/* 中英文文本 */
void LCD_SetTextFont(pFONT *fonts);
void LCD_DisplayChinese(uint16_t x, uint16_t y, const char *pText);
void LCD_DisplayText(uint16_t x, uint16_t y, const char *pText);

/* 数字/小数 */
void LCD_ShowNumMode(uint8_t mode);
void LCD_DisplayNumber(uint16_t x, uint16_t y, int32_t number, uint8_t len);
void LCD_DisplayDecimals(uint16_t x, uint16_t y, double decimals, uint8_t len, uint8_t decs);

/* 2D 图形 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color);
void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height);
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_DrawEllipse(int x, int y, int r1, int r2);

/* 填充 */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r);

/* 图片 */
void LCD_DrawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *pImage);

#endif /* MOD_LCD_H_ */
