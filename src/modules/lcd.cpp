/**
 * @file    lcd.cpp
 * @brief   ST7789 SPI LCD 驱动 (SPI1, 240x240 RGB565)
 * @note    PB3=AF5(SPI1_SCK), PB5=AF5(SPI1_MOSI), PE2=DC, PE3=CS, PE4=BLK
 *          SPI1 位于 APB2 (PCLK2=84MHz), 1线TX模式, 10.5MHz
 */

#include "lcd.h"
#include "app/app_debug.h"
#include "modbus/modbus_core.h"
#include <FreeRTOS.h>
#include <task.h>

/* ========================== 静态字体指针 ========================== */
static pFONT *LCD_AsciiFonts;
static pFONT *LCD_CHFonts;

/* ========================== 显存缓冲区 ========================== */
uint16_t LCD_Buff[1024];

/* ========================== LCD 参数结构体 ========================== */
struct {
    uint32_t Color;
    uint32_t BackColor;
    uint8_t  ShowNum_Mode;
    uint8_t  Direction;
    uint16_t Width;
    uint16_t Height;
} LCD;

/* ========================== GPIO 宏 ========================== */
#define LCD_DC_H()    (GPIOE->BSRR = GPIO_PIN_2)
#define LCD_DC_L()    (GPIOE->BSRR = (uint32_t)GPIO_PIN_2 << 16)
#define LCD_CS_H()    (GPIOE->BSRR = GPIO_PIN_3)
#define LCD_CS_L()    (GPIOE->BSRR = (uint32_t)GPIO_PIN_3 << 16)
#define LCD_BLK_H()   (GPIOE->BSRR = GPIO_PIN_4)
#define LCD_BLK_L()   (GPIOE->BSRR = (uint32_t)GPIO_PIN_4 << 16)

/* ========================== local helpers ========================== */
static uint16_t RGB888_to_RGB565(uint32_t color) {
    return (uint16_t)(((color >> 19) & 0x1F) << 11)
         | (uint16_t)(((color >> 10) & 0x3F) << 5)
         | (uint16_t)((color >> 3) & 0x1F);
}

static void LCD_WriteCommand(uint8_t cmd) {
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    LCD_DC_L();
    SPI1->DR = cmd;
    while ((SPI1->SR & SPI_SR_TXE) == RESET);
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    LCD_DC_H();
}

static void LCD_WriteData_8bit(uint8_t data) {
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    SPI1->DR = data;
    while ((SPI1->SR & SPI_SR_TXE) == RESET);
}

static void LCD_WriteData_16bit(uint16_t data) {
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    SPI1->DR = data;
    while ((SPI1->SR & SPI_SR_TXE) == RESET);
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;
}

static void LCD_WriteBuff(const uint16_t *pBuff, uint32_t size) {
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;
    for (uint32_t i = 0; i < size; i++) {
        while ((SPI1->SR & SPI_SR_TXE) == RESET);
        SPI1->DR = pBuff[i];
    }
    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;
}

static void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    /* 全程 8-bit 模式, 避免 LCD_WriteData_16bit 的 SPE 反复开关
     * 导致 ST7789 误读 SPI 间隙数据产生条纹 */
    LCD_WriteCommand(0x2A); /* column */
    LCD_WriteData_8bit(x1 >> 8);
    LCD_WriteData_8bit(x1 & 0xFF);
    LCD_WriteData_8bit(x2 >> 8);
    LCD_WriteData_8bit(x2 & 0xFF);
    LCD_WriteCommand(0x2B); /* row */
    LCD_WriteData_8bit(y1 >> 8);
    LCD_WriteData_8bit(y1 & 0xFF);
    LCD_WriteData_8bit(y2 >> 8);
    LCD_WriteData_8bit(y2 & 0xFF);
    LCD_WriteCommand(0x2C); /* RAM write */
}

/* ========================== 背光控制 ========================== */
void LCD_BLK_ON(void)  { LCD_BLK_H(); }
void LCD_BLK_OFF(void) { LCD_BLK_L(); }

/* ========================== 颜色/方向/字体设置 ========================== */
void LCD_SetColor(uint32_t Color)    { LCD.Color = RGB888_to_RGB565(Color); }
void LCD_SetBackColor(uint32_t Color) { LCD.BackColor = RGB888_to_RGB565(Color); }
void LCD_SetAsciiFont(pFONT *fonts)  { LCD_AsciiFonts = fonts; }
void LCD_SetTextFont(pFONT *fonts)   { LCD_CHFonts = fonts; }
void LCD_ShowNumMode(uint8_t mode)   { LCD.ShowNum_Mode = mode; }

void LCD_SetDirection(uint8_t direction) {
    LCD.Direction = direction;
    LCD_WriteCommand(0x36);
    if (direction == Direction_V)
        LCD_WriteData_8bit(0x00);
    else if (direction == Direction_V_Flip)
        LCD_WriteData_8bit(0x80);
    else if (direction == Direction_H)
        LCD_WriteData_8bit(0x60);
    else
        LCD_WriteData_8bit(0xA0);
}

/* ========================== 清屏 ========================== */
static void LCD_Clear_Base(uint32_t color) {
    LCD_CS_L();
    LCD_SetAddress(0, 0, LCD_Width - 1, LCD_Height - 1);

    /* 切换到16-bit模式, 全程不切换, 避免 SPI 间隙导致花屏 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    uint16_t c = RGB888_to_RGB565(color);
    uint32_t total = (uint32_t)LCD_Width * LCD_Height;
    uint32_t sent = 0;
    while (sent < total) {
        uint32_t chunk = (total - sent) > 1024 ? 1024 : (total - sent);
        for (uint32_t i = 0; i < chunk; i++) LCD_Buff[i] = c;
        for (uint32_t i = 0; i < chunk; i++) {
            while ((SPI1->SR & SPI_SR_TXE) == RESET);
            SPI1->DR = LCD_Buff[i];
        }
        sent += chunk;
    }

    while ((SPI1->SR & SPI_SR_BSY) != RESET);

    /* 恢复8-bit模式 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    LCD_CS_H();
}

void LCD_Clear(void)           { LCD_Clear_Base(LCD.BackColor); }
void LCD_SetBackColor_Clear(void) { LCD_Clear_Base(LCD.BackColor & 0xFFFF); }

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    LCD_CS_L();
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);

    /* 切换到16-bit模式, 全程不切换 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    uint32_t total = (uint32_t)width * height;
    uint32_t sent = 0;
    while (sent < total) {
        uint32_t chunk = (total - sent) > 1024 ? 1024 : (total - sent);
        for (uint32_t i = 0; i < chunk; i++) LCD_Buff[i] = LCD.BackColor;
        for (uint32_t i = 0; i < chunk; i++) {
            while ((SPI1->SR & SPI_SR_TXE) == RESET);
            SPI1->DR = LCD_Buff[i];
        }
        sent += chunk;
    }

    while ((SPI1->SR & SPI_SR_BSY) != RESET);

    /* 恢复8-bit模式 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    LCD_CS_H();
}

/* ========================== 填充矩形 ========================== */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    LCD_CS_L();
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);

    /* 切换到16-bit模式, 全程不切换, 避免 SPI 间隙导致花屏 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    uint32_t total = (uint32_t)width * height;
    uint32_t sent = 0;
    while (sent < total) {
        uint32_t chunk = (total - sent) > 1024 ? 1024 : (total - sent);
        for (uint32_t i = 0; i < chunk; i++) LCD_Buff[i] = LCD.Color;
        for (uint32_t i = 0; i < chunk; i++) {
            while ((SPI1->SR & SPI_SR_TXE) == RESET);
            SPI1->DR = LCD_Buff[i];
        }
        sent += chunk;
    }

    while ((SPI1->SR & SPI_SR_BSY) != RESET);

    /* 恢复8-bit模式 */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_DATASIZE_16BIT;
    SPI1->CR1 |= SPI_CR1_SPE;

    LCD_CS_H();
}

/* ========================== 画点 ========================== */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color_unused) {
    (void)color_unused;
    LCD_CS_L();
    LCD_SetAddress(x, y, x, y);
    LCD_WriteData_16bit(LCD.Color);
    LCD_CS_H();
}

/* ========================== ASCII 字符显示 ========================== */
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c) {
    if (!LCD_AsciiFonts) return;
    uint16_t w = LCD_AsciiFonts->Width;
    uint16_t h = LCD_AsciiFonts->Height;
    uint16_t total_bytes = LCD_AsciiFonts->Sizes;
    const uint8_t *pTable = LCD_AsciiFonts->pTable;

    /* 计算每行字节数: ceil(w/8), 例如 12px 宽 = 2 字节/行 */
    uint16_t bytes_per_row = (w + 7) / 8;

    LCD_CS_L();
    LCD_SetAddress(x, y, x + w - 1, y + h - 1);

    uint32_t offset = (uint32_t)(c - 0x20) * total_bytes;
    for (uint16_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            uint8_t byte_idx = col / 8;
            uint8_t bit_idx  = (col % 8);  /* LSB-first: bit0=最左像素 */
            uint8_t row_data = pTable[offset + row * bytes_per_row + byte_idx];
            if (row_data & (1 << bit_idx))
                LCD_Buff[row * w + col] = LCD.Color;
            else
                LCD_Buff[row * w + col] = LCD.BackColor;
        }
    }
    LCD_WriteBuff(LCD_Buff, w * h);
    LCD_CS_H();
}

void LCD_DisplayString(uint16_t x, uint16_t y, const char *p) {
    if (!p || !LCD_AsciiFonts) return;
    uint16_t cx = x;
    while (*p) {
        if ((uint8_t)*p < 0x20 || (uint8_t)*p > 0x7E) { p++; continue; }
        LCD_DisplayChar(cx, y, (uint8_t)*p);
        cx += LCD_AsciiFonts->Width;
        if (cx + LCD_AsciiFonts->Width > LCD_Width) {
            cx = x;
            y += LCD_AsciiFonts->Height;
        }
        p++;
    }
}

/* ========================== 数字显示 ========================== */
void LCD_DisplayNumber(uint16_t x, uint16_t y, int32_t number, uint8_t len) {
    char buf[12];
    if (LCD.ShowNum_Mode == Fill_Zero)
        snprintf(buf, sizeof(buf), "%0*d", len, (int)number);
    else
        snprintf(buf, sizeof(buf), "%*d", len, (int)number);
    LCD_DisplayString(x, y, buf);
}

void LCD_DisplayDecimals(uint16_t x, uint16_t y, double decimals, uint8_t int_len, uint8_t dec_len) {
    char buf[20];
    int int_part = (int)decimals;
    int frac_part = (int)((decimals - int_part) * (dec_len == 1 ? 10.0 : 100.0) + 0.5);
    if (frac_part < 0) frac_part = -frac_part;
    if (LCD.ShowNum_Mode == Fill_Zero)
        snprintf(buf, sizeof(buf), "%0*d.%0*d", int_len, int_part, dec_len, frac_part);
    else
        snprintf(buf, sizeof(buf), "%*d.%0*d", int_len, int_part, dec_len, frac_part);
    LCD_DisplayString(x, y, buf);
}

/* ========================== 线条绘制 ========================== */
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width) {
    LCD_FillRect(x, y, width, 1);
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height) {
    LCD_FillRect(x, y, 1, height);
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    int dx = (int)x2 - (int)x1;
    int dy = (int)y2 - (int)y1;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps == 0) { LCD_DrawPoint(x1, y1, 0); return; }
    float xi = (float)dx / steps;
    float yi = (float)dy / steps;
    float xf = (float)x1, yf = (float)y1;
    for (int i = 0; i <= steps; i++) {
        LCD_DrawPoint((uint16_t)(xf + 0.5f), (uint16_t)(yf + 0.5f), 0);
        xf += xi; yf += yi;
    }
}

/* ========================== 矩形/圆/椭圆 ========================== */
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    LCD_DrawLine_H(x, y, width);
    LCD_DrawLine_H(x, y + height - 1, width);
    LCD_DrawLine_V(x, y, height);
    LCD_DrawLine_V(x + width - 1, y, height);
}

void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r) {
    int a = 0, b = r;
    while (a <= b) {
        LCD_DrawPoint(x + a, y - b, 0); LCD_DrawPoint(x - a, y - b, 0);
        LCD_DrawPoint(x + a, y + b, 0); LCD_DrawPoint(x - a, y + b, 0);
        LCD_DrawPoint(x + b, y - a, 0); LCD_DrawPoint(x - b, y - a, 0);
        LCD_DrawPoint(x + b, y + a, 0); LCD_DrawPoint(x - b, y + a, 0);
        a++;
        if (a * a + b * b > r * r) b--;
    }
}

void LCD_DrawEllipse(int x, int y, int r1, int r2) {
    float xi, yi;
    for (int i = 0; i < 628; i++) {
        xi = (float)(r1 * cos((double)i / 100.0));
        yi = (float)(r2 * sin((double)i / 100.0));
        LCD_DrawPoint(x + (int)(xi + 0.5f), y + (int)(yi + 0.5f), 0);
    }
}

/* ========================== 填充圆 ========================== */
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r) {
    for (int i = 0; i <= r; i++) {
        int w = (int)sqrt(r * r - i * i);
        LCD_FillRect(x - w, y - i, 2 * w, 1);
        if (i > 0) LCD_FillRect(x - w, y + i, 2 * w, 1);
    }
}

/* ========================== 中文显示 (GB2312 字库, 16-16 编码) ========================== */
void LCD_DisplayChinese(uint16_t x, uint16_t y, const char *pText) {
    (void)x; (void)y; (void)pText;
    /* 注: 当前未使用中文, 保留接口 */
}

void LCD_DisplayText(uint16_t x, uint16_t y, const char *pText) {
    /* 简化: 仅显示 ASCII, 中文暂跳过 */
    LCD_DisplayString(x, y, pText);
}

/* ========================== 图片显示 ========================== */
void LCD_DrawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *pImage) {
    LCD_CS_L();
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    uint32_t total = (uint32_t)width * height;
    uint32_t sent = 0;
    while (sent < total) {
        uint32_t chunk = (total - sent) > 1024 ? 1024 : (total - sent);
        for (uint32_t i = 0; i < chunk; i++) {
            LCD_Buff[i] = ((uint16_t)pImage[(sent + i) * 2] << 8) | pImage[(sent + i) * 2 + 1];
        }
        LCD_WriteBuff(LCD_Buff, chunk);
        sent += chunk;
    }
    LCD_CS_H();
}

/* ========================== SPI1 初始化 (纯寄存器) ========================== */
void SPI_LCD_Init(void) {
    DBG("LCD", "===== SPI_LCD_Init START =====");

    /* ── 1. 时钟使能 ── */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* ── 2. GPIO 配置 (PB3=SCK, PB5=MOSI, PE2=DC, PE3=CS, PE4=BLK) ── */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PB3: SPI1_SCK, PB5: SPI1_MOSI */
    GPIO_InitStruct.Pin       = GPIO_PIN_3 | GPIO_PIN_5;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PE2=DC, PE3=CS, PE4=BLK */
    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    LCD_CS_H();
    LCD_DC_H();
    LCD_BLK_L();  /* 初始化期间背光关 */
    DBG("LCD", "1.GPIO done: PB3=SCK PB5=MOSI PE2=DC PE3=CS PE4=BLK");

    /* ── 3. SPI1 复位 + 配置 ── */
    RCC->APB2RSTR |=  (1UL << 12);   /* 复位 SPI1 */
    RCC->APB2RSTR &= ~(1UL << 12);

    /* CR1: BIDIMODE=1, BIDIOE=1, MSTR=1, SSM=1, SSI=1, BR=010(10.5MHz) */
    SPI1->CR1 = (1UL << 15) | (1UL << 14) | (1UL << 2) | (1UL << 9) | (1UL << 8) | (2UL << 3);
    SPI1->CR2 = 0;
    SPI1->CR1 |= (1UL << 6);         /* SPE=1 */
    DBG("LCD", "SPI1 direct reg init OK, 10.5MHz (BR=010)");
    DBG("LCD", "2.SPI1 config done");

    /* ── 5. 延时 ── */
    vTaskDelay(pdMS_TO_TICKS(5));
    DBG("LCD", "3.Delay 5ms done, start init seq");

    /* ── 6. ST7789 初始化序列 (对齐例程) ── */
    LCD_CS_L();

    LCD_WriteCommand(0x36); LCD_WriteData_8bit(0x00);  /* MADCTL */
    LCD_WriteCommand(0x3A); LCD_WriteData_8bit(0x05);  /* 16-bit RGB565 */

    LCD_WriteCommand(0xB2);  /* PORCTRL */
    LCD_WriteData_8bit(0x0C); LCD_WriteData_8bit(0x0C);
    LCD_WriteData_8bit(0x00); LCD_WriteData_8bit(0x33);
    LCD_WriteData_8bit(0x33);

    LCD_WriteCommand(0xB7); LCD_WriteData_8bit(0x35);  /* VGH/VGL */
    LCD_WriteCommand(0xBB); LCD_WriteData_8bit(0x19);  /* VCOM */
    LCD_WriteCommand(0xC0); LCD_WriteData_8bit(0x2C);
    LCD_WriteCommand(0xC2); LCD_WriteData_8bit(0x01);
    LCD_WriteCommand(0xC3); LCD_WriteData_8bit(0x12);  /* VRH */
    LCD_WriteCommand(0xC4); LCD_WriteData_8bit(0x20);  /* VDV */
    LCD_WriteCommand(0xC6); LCD_WriteData_8bit(0x0F);  /* 帧率 */

    LCD_WriteCommand(0xD0);  /* 电源控制 */
    LCD_WriteData_8bit(0xA4); LCD_WriteData_8bit(0xA1);

    /* 正极性伽马 */
    LCD_WriteCommand(0xE0);
    LCD_WriteData_8bit(0xD0); LCD_WriteData_8bit(0x04);
    LCD_WriteData_8bit(0x0D); LCD_WriteData_8bit(0x11);
    LCD_WriteData_8bit(0x13); LCD_WriteData_8bit(0x2B);
    LCD_WriteData_8bit(0x3F); LCD_WriteData_8bit(0x54);
    LCD_WriteData_8bit(0x4C); LCD_WriteData_8bit(0x18);
    LCD_WriteData_8bit(0x0D); LCD_WriteData_8bit(0x0B);
    LCD_WriteData_8bit(0x1F); LCD_WriteData_8bit(0x23);

    /* 负极性伽马 */
    LCD_WriteCommand(0xE1);
    LCD_WriteData_8bit(0xD0); LCD_WriteData_8bit(0x04);
    LCD_WriteData_8bit(0x0C); LCD_WriteData_8bit(0x11);
    LCD_WriteData_8bit(0x13); LCD_WriteData_8bit(0x2C);
    LCD_WriteData_8bit(0x3F); LCD_WriteData_8bit(0x44);
    LCD_WriteData_8bit(0x51); LCD_WriteData_8bit(0x2F);
    LCD_WriteData_8bit(0x1F); LCD_WriteData_8bit(0x1F);
    LCD_WriteData_8bit(0x20); LCD_WriteData_8bit(0x23);

    LCD_WriteCommand(0x21);  /* INVON */
    LCD_WriteCommand(0x11);  /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));
    LCD_WriteCommand(0x29);  /* DISPON */

    while ((SPI1->SR & SPI_SR_BSY) != RESET);
    LCD_CS_H();
    DBG("LCD", "Full init seq done");

    /* ── 7. 默认设置 ── */
    LCD.Direction   = Direction_V;
    LCD.Width       = LCD_Width;
    LCD.Height      = LCD_Height;
    LCD.Color       = LCD_WHITE;
    LCD.BackColor   = LCD_BLACK;
    LCD.ShowNum_Mode = Fill_Zero;
    DBG("LCD", "7.defaults set");
    DEBUG_SERIAL.flush();

    LCD_SetBackColor(LCD_BLACK);
    LCD_SetColor(LCD_WHITE);
    LCD_SetAsciiFont(&ASCII_Font24);
    DBG("LCD", "7b.font set");
    DEBUG_SERIAL.flush();

    /* 测试: 开背光 + 全屏填充红色 + 延时1秒 */
    LCD_BLK_ON();
    DBG("LCD", "7c.BLK ON, filling red...");
    DEBUG_SERIAL.flush();
    LCD_SetColor(LCD_RED);
    LCD_FillRect(0, 0, 240, 240);
    DBG("LCD", "7d.red fill done, delay 1s...");
    DEBUG_SERIAL.flush();
    vTaskDelay(pdMS_TO_TICKS(1000));
    DBG("LCD", "7e.delay done");
    DEBUG_SERIAL.flush();

    /* ── 8. 诊断 ── */
    {
        uint32_t odr = GPIOE->ODR;
        uint32_t cr1 = SPI1->CR1;
        uint32_t sr  = SPI1->SR;
        uint32_t af3 = (GPIOB->AFR[0] >> 12) & 0xF;
        uint32_t af5 = (GPIOB->AFR[0] >> 20) & 0xF;
        uint32_t moder = GPIOB->MODER;
        DBG("LCD", "===== SPI_LCD_Init DONE: BLK=ON =====");
        DEBUG_SERIAL.print("[LCD.DIAG] GPIOE.ODR=0x");
        DEBUG_SERIAL.print(odr, HEX);
        DEBUG_SERIAL.print(" (PE2="); DEBUG_SERIAL.print((odr >> 2) & 1);
        DEBUG_SERIAL.print(" PE3=");  DEBUG_SERIAL.print((odr >> 3) & 1);
        DEBUG_SERIAL.print(" PE4=");  DEBUG_SERIAL.print((odr >> 4) & 1);
        DEBUG_SERIAL.print(") SPI1.CR1=0x"); DEBUG_SERIAL.print(cr1, HEX);
        DEBUG_SERIAL.print(" SR=0x"); DEBUG_SERIAL.print(sr, HEX);
        DEBUG_SERIAL.print(" PB3_AF="); DEBUG_SERIAL.print(af3);
        DEBUG_SERIAL.print(" PB5_AF="); DEBUG_SERIAL.print(af5);
        DEBUG_SERIAL.print(" PB_MODER=0x"); DEBUG_SERIAL.print(moder, HEX);
        DEBUG_SERIAL.println();
    }
}

/* ========================== LCD 显示任务 ========================== */
void lcd_task(void *pvParameters) {
    (void)pvParameters;
    DBG("LCD", "TASK started, init LCD hardware...");
    DEBUG_SERIAL.flush();

    SPI_LCD_Init();

    DBG("LCD", "TASK init done, entering display loop");
    DEBUG_SERIAL.flush();

    LCD_SetAsciiFont(&ASCII_Font24);

    /* ── 一次性全屏清黑 + 绘制静态内容 ── */
    LCD_SetBackColor(LCD_BLACK);
    LCD_Clear();

    LCD_SetColor(LCD_WHITE);
    LCD_DisplayString(5, 5, "Remote IO V4");

    LCD_SetColor(LCD_GREY);
    LCD_DrawLine_H(5, 30, 230);
    LCD_DrawLine_H(5, 160, 230);

    LCD_SetColor(LCD_CYAN);
    LCD_DisplayString(5, 40,  "Temp:");
    LCD_DisplayString(5, 70,  "Humi:");
    LCD_DisplayString(5, 100, "CO2 :");
    LCD_DisplayString(5, 130, "NH3 :");
    LCD_DisplayString(5, 170, "Fan: ");
    LCD_DisplayString(130, 170, "Hum: ");

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint16_t last_status = 0xFFFF;
    uint16_t last_output = 0xFFFF;
    int16_t  last_temp = -32768, last_humi = -32768;
    uint16_t last_co2 = 0xFFFF;
    int16_t  last_nh3 = -32768;

    while (1) {
        uint16_t status = modbus_reg_get(REG_ESP32_STATUS);
        uint16_t output = modbus_reg_get(REG_OUTPUT_STATE);
        bool status_changed = (status != last_status);
        bool output_changed = (output != last_output);

        /* ─── 温度: 清除区域 + 重绘 ─── */
        int16_t temp_raw = (int16_t)modbus_reg_get(REG_TEMP_X100);
        if (status_changed || temp_raw != last_temp) {
            LCD_ClearRect(90, 40, 105, 24);
            LCD_SetColor(LCD_WHITE);
            if (status != 0xFFFF && status != 0) {
                double temp = temp_raw / 100.0;
                LCD_DisplayDecimals(90, 40, temp, 3, 1);
                LCD_DisplayString(150, 40, "C");
            } else {
                LCD_DisplayString(90, 40, "---.- C");
            }
            last_temp = temp_raw;
        }

        /* ─── 湿度 ─── */
        int16_t humi_raw = (int16_t)modbus_reg_get(REG_HUMI_X100);
        if (status_changed || humi_raw != last_humi) {
            LCD_ClearRect(90, 70, 105, 24);
            LCD_SetColor(LCD_WHITE);
            if (status != 0xFFFF && status != 0) {
                double humi = humi_raw / 100.0;
                LCD_DisplayDecimals(90, 70, humi, 3, 1);
                LCD_DisplayString(150, 70, "%");
            } else {
                LCD_DisplayString(90, 70, "---.- %");
            }
            last_humi = humi_raw;
        }

        /* ─── CO2 ─── */
        uint16_t co2 = modbus_reg_get(REG_CO2);
        if (status_changed || co2 != last_co2) {
            LCD_ClearRect(90, 100, 120, 24);
            LCD_SetColor(LCD_WHITE);
            if (status != 0xFFFF && status != 0) {
                LCD_DisplayNumber(90, 100, co2, 4);
                LCD_DisplayString(138, 100, "ppm");
            } else {
                LCD_DisplayString(90, 100, "---- ppm");
            }
            last_co2 = co2;
        }

        /* ─── NH3 ─── */
        int16_t nh3_raw = (int16_t)modbus_reg_get(REG_NH3_X100);
        if (status_changed || nh3_raw != last_nh3) {
            LCD_ClearRect(90, 130, 120, 24);
            LCD_SetColor(LCD_WHITE);
            if (status != 0xFFFF && status != 0) {
                double nh3 = nh3_raw / 100.0;
                LCD_DisplayDecimals(90, 130, nh3, 3, 1);
                LCD_DisplayString(150, 130, "ppm");
            } else {
                LCD_DisplayString(90, 130, "---.- ppm");
            }
            last_nh3 = nh3_raw;
        }

        /* ─── 风机 ─── */
        if (status_changed || output_changed) {
            LCD_ClearRect(90, 170, 50, 24);
            if (output & 0x0F) {
                LCD_SetColor(LCD_GREEN);
                LCD_DisplayString(90, 170, "ON ");
            } else {
                LCD_SetColor(LCD_RED);
                LCD_DisplayString(90, 170, "OFF");
            }
        }

        /* ─── 加湿器 ─── */
        if (status_changed || output_changed) {
            LCD_ClearRect(200, 170, 50, 24);
            if (output & 0xF0) {
                LCD_SetColor(LCD_GREEN);
                LCD_DisplayString(200, 170, "ON ");
            } else {
                LCD_SetColor(LCD_RED);
                LCD_DisplayString(200, 170, "OFF");
            }
        }

        last_status = status;
        last_output = output;

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
    }
}

/* ========================== 外部入口 ========================== */
void lcd_init(void) {
    SPI_LCD_Init();
}
