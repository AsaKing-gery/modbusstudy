/**
 * @file    esp32.cpp
 * @brief   ESP32 SPI 从机驱动 (SPI2, 1MHz MODE0, 硬件 NSS)
 * @note    STM32 作为 SPI Slave，通过轮询 SPI2->SR 接收 ESP32 Master 的 14 字节传感器帧
 *          协议: [0xAA][0x55][0x01][temp:2BE][humi:2BE][co2:2BE][nh3:2BE][lux:2BE][crc8]
 *          不依赖 HAL 中断回调（中断在 FreeRTOS 下时序不可靠），改用轮询 RXNE
 */

#include "esp32.h"
#include "modbus/modbus_core.h"
#include "bsp/bsp_debug.h"

/* ========================== SPI2 HAL 句柄 ========================== */
static SPI_HandleTypeDef hspi2;

/* ========================== 接收缓冲区 ========================== */
static uint8_t  spi_rx_buf[ESP32_SPI_FRAME_LEN];   /**< 从 ESP32 收到的原始帧 */

/* ========================== 运行状态 ========================== */
static volatile uint16_t esp32_status = 0;
static volatile uint8_t  spi_crc_errors = 0;

/* ========================== 解析后的传感器数据 ========================== */
static Esp32SensorData g_esp32_data;

/* ========================== GPIO 初始化 ========================== */
static void esp32_gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = ESP32_SPI_AF;           /* GPIO_AF5_SPI2 */

    /* PB13=SCK, PB14=MISO, PB15=MOSI */
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB12=NSS (硬件 NSS, 上拉防止误触发) */
    gpio.Pin  = GPIO_PIN_12;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

/* ========================== 帧处理 ========================== */

static inline int16_t be16_to_i16(const uint8_t *buf)
{
    return (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static uint8_t calc_checksum(const uint8_t *frame)
{
    uint8_t sum = 0;
    for (int i = 2; i < 13; i++) {
        sum += frame[i];
    }
    return sum;
}

static bool esp32_parse_frame(const uint8_t *frame)
{
    if (frame[0] != ESP32_FRAME_HEADER0 || frame[1] != ESP32_FRAME_HEADER1) {
        DBG("ESP32", "frame header mismatch: "
            + String(frame[0], HEX) + " " + String(frame[1], HEX));
        return false;
    }

    if (frame[13] != calc_checksum(frame)) {
        spi_crc_errors++;
        return false;
    }

    g_esp32_data.temp_x100 = be16_to_i16(&frame[3]);
    g_esp32_data.humi_x100 = be16_to_i16(&frame[5]);
    g_esp32_data.co2       = be16_to_i16(&frame[7]);
    g_esp32_data.nh3_x100  = be16_to_i16(&frame[9]);
    g_esp32_data.lux_x100  = be16_to_i16(&frame[11]);
    g_esp32_data.valid     = true;
    g_esp32_data.update_ms = millis();

    return true;
}

/* ========================== 初始化 ========================== */

void esp32_init(void)
{
    TRACE("E");

    memset(&g_esp32_data, 0, sizeof(g_esp32_data));

    esp32_gpio_init();

    __HAL_RCC_SPI2_CLK_ENABLE();

    hspi2.Instance               = SPI2;
    hspi2.Init.Mode              = SPI_MODE_SLAVE;
    hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi2.Init.NSS               = SPI_NSS_HARD_INPUT;
    hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial     = 10;

    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        TRACE_LN(" ESP32 SPI2 HAL init failed!");
        return;
    }

    /* 使能 SPI2 (不启用中断, 完全轮询) */
    __HAL_SPI_ENABLE(&hspi2);

    esp32_status |= 0x01;
    TRACE("e");
    DBG("ESP32", "SPI slave poll init OK");
}

/* ========================== 轮询收帧 ========================== */

/**
 * @brief 轮询方式从 SPI2 接收完整帧
 * @note  NSS=LO → 等待 RXNE → 读 DR → 直到 NSS=HI 或满 14 字节
 *        不依赖 HAL 中断, 在 FreeRTOS 任务中安全调用
 * @return 收到的字节数
 */
static uint8_t spi_poll_rx_frame(uint8_t *buf, uint8_t max_len)
{
    /* 快速检测 NSS: 只检查一次, 不阻塞 */
    if (GPIOB->IDR & GPIO_PIN_12) {
        return 0;  /* NSS 高, 主站未选中, 立即返回 */
    }

    uint8_t count = 0;

    /* 清 OVR (上次可能残留) */
    if (SPI2->SR & SPI_SR_OVR) {
        volatile uint8_t dummy = SPI2->DR;
        (void)dummy;
        __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
    }

    /* NSS 低电平期间逐个接收字节 */
    while (count < max_len) {
        /* 等待 RXNE, 快速自旋 (~100us 超时) */
        uint32_t byte_timeout = 20000;
        while (!(SPI2->SR & SPI_SR_RXNE) && byte_timeout > 0) {
            byte_timeout--;
        }
        if (byte_timeout == 0) break;

        buf[count++] = (uint8_t)(SPI2->DR);

        /* NSS 恢复高电平 → 帧结束 */
        if (GPIOB->IDR & GPIO_PIN_12) break;
    }

    return count;
}

/* ========================== 任务 ========================== */

void esp32_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    DBG("ESP32", "task started (poll mode)");

    uint32_t last_heartbeat = 0;
    uint32_t frame_count = 0;

    while (true) {
        /* ─── 轮询收帧 ─── */
        uint8_t len = spi_poll_rx_frame(spi_rx_buf, ESP32_SPI_FRAME_LEN);

        if (len == ESP32_SPI_FRAME_LEN) {
            frame_count++;

            DEBUG_SERIAL.print("[ESP32.RX] HEX: ");
            for (int i = 0; i < len; i++) {
                if (spi_rx_buf[i] < 0x10) DEBUG_SERIAL.print('0');
                DEBUG_SERIAL.print(spi_rx_buf[i], HEX);
                DEBUG_SERIAL.print(' ');
            }
            DEBUG_SERIAL.println();
            DEBUG_SERIAL.flush();

            if (esp32_parse_frame(spi_rx_buf)) {
                esp32_status |= 0x02;  /* 标记收到有效帧 */
                modbus_reg_set(REG_TEMP_X100,  (uint16_t)g_esp32_data.temp_x100);
                modbus_reg_set(REG_HUMI_X100,  (uint16_t)g_esp32_data.humi_x100);
                modbus_reg_set(REG_CO2,        (uint16_t)g_esp32_data.co2);
                modbus_reg_set(REG_NH3_X100,   (uint16_t)g_esp32_data.nh3_x100);
                modbus_reg_set(REG_LUX_X100,   (uint16_t)g_esp32_data.lux_x100);

                DEBUG_SERIAL.print("[ESP32] T="); DEBUG_SERIAL.print(g_esp32_data.temp_x100 / 100);
                DEBUG_SERIAL.print("."); DEBUG_SERIAL.print(abs(g_esp32_data.temp_x100) % 100);
                DEBUG_SERIAL.print(" H="); DEBUG_SERIAL.print(g_esp32_data.humi_x100 / 100);
                DEBUG_SERIAL.print("."); DEBUG_SERIAL.print(abs(g_esp32_data.humi_x100) % 100);
                DEBUG_SERIAL.print(" CO2="); DEBUG_SERIAL.print(g_esp32_data.co2);
                DEBUG_SERIAL.print(" NH3="); DEBUG_SERIAL.print(g_esp32_data.nh3_x100 / 100);
                DEBUG_SERIAL.print("."); DEBUG_SERIAL.print(abs(g_esp32_data.nh3_x100) % 100);
                DEBUG_SERIAL.println();
                DEBUG_SERIAL.flush();
            } else {
                DEBUG_SERIAL.println("[ESP32.RX] frame parse FAILED");
                DEBUG_SERIAL.flush();
            }
        }

        /* ─── 诊断心跳 ─── */
        if (millis() - last_heartbeat >= 30000) {
            last_heartbeat = millis();
            uint16_t sr = SPI2->SR;
            bool nss = (GPIOB->IDR & GPIO_PIN_12) != 0;
            DEBUG_SERIAL.print("[ESP32.DIAG] SR=0x"); DEBUG_SERIAL.print(sr, HEX);
            DEBUG_SERIAL.print(" NSS="); DEBUG_SERIAL.print(nss ? "HI" : "LO");
            DEBUG_SERIAL.print(" frames="); DEBUG_SERIAL.print(frame_count);
            DEBUG_SERIAL.print(" crc_err="); DEBUG_SERIAL.print(spi_crc_errors);
            DEBUG_SERIAL.println();
            DEBUG_SERIAL.flush();
        }

        modbus_reg_set(REG_ESP32_STATUS, esp32_status);

        /* 有数据时 1ms 轮询, 无数据时 20ms 释放 CPU 给 LCD(prio3) 等任务 */
        vTaskDelay(pdMS_TO_TICKS((len == ESP32_SPI_FRAME_LEN) ? 1 : 20));
    }
}

/* ========================== 公共接口 ========================== */

uint16_t esp32_get_status(void)
{
    return esp32_status;
}

void esp32_get_data(Esp32SensorData *out)
{
    if (out) {
        memcpy(out, &g_esp32_data, sizeof(Esp32SensorData));
    }
}
