// User_Setup.h - TFT_eSPI configuration for STM32F407VET6 + ST7789 240x240
// Pins: PB3=SCK, PB5=MOSI (SPI1), PE2=DC, PE3=CS, PE4=BLK

#define USER_SETUP_INFO "STM32F407VET6 ST7789 240x240 SPI1"

// ── Driver ──
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ── SPI Pins (SPI1) ──
#define TFT_MISO -1     // Not used (write-only)
#define TFT_MOSI PB5
#define TFT_SCLK PB3
#define TFT_CS   PE3
#define TFT_DC   PE2
#define TFT_RST  -1     // Not connected

// ── Backlight ──
#define TFT_BL            PE4
#define TFT_BACKLIGHT_ON  HIGH

// ── SPI Frequency (SPI1 on APB2 @ 84MHz) ──
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// ── Fonts ──
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_GFXFF
#define SMOOTH_FONT
