/**
 * @file    esp32.cpp
 * @brief   ESP32 SPI 从机通信模块 (STM32侧) — 工业级握手协议
 *
 * @note    SPI2 从机模式, DMA 循环模式接收 (DMA1_Stream3 Ch0 RX, DMA1_Stream4 Ch0 TX)
 *          SPI2: SCK=PB13, MISO=PB14, MOSI=PB15, NSS=PB12 (AF5)
 *          模式: 从机, 1MHz, CPOL=0/CPHA=0 (MODE0), MSB first, 8bit
 *          FIFO 使能 (4级), 降低过载风险
 *
 * 帧检测:
 *   NSS 下降沿 → 标记帧开始
 *   DMA 循环模式自动将数据写入环缓冲
 *   NSS 上升沿 → 读 NDTR 计算帧长, 拷贝到线缓冲, FreeRTOS 通知任务处理
 *
 * 握手流程:
 *   1. STM32 启动 → HS_INIT, 等待 ESP32 的 HANDSHAKE_REQ
 *   2. 收到 HANDSHAKE_REQ → HS_RESPONSE, MISO 循环输出 9B 响应帧
 *   3. 收到 HANDSHAKE_ACK → HS_READY, 正常数据交换
 *   4. 定时检测心跳超时 → HS_INIT (重新等待握手)
 */

#include "modules/esp32.h"
#include "app/ota.h"
#include "modbus/modbus_rtu.h"
#include "bsp/bsp_debug.h"
#include "stm32f4xx_hal.h"

/* ========================== 硬件配置 ========================== */
#define ESP_SPI             SPI2
#define ESP_SPI_AF          GPIO_AF5_SPI2
#define ESP_SPI_PORT        GPIOB
#define ESP_PIN_SCK         GPIO_PIN_13
#define ESP_PIN_MISO        GPIO_PIN_14
#define ESP_PIN_MOSI        GPIO_PIN_15
#define ESP_PIN_NSS         GPIO_PIN_12
#define ESP_PIN_NSS_PIN     PB12              /* Arduino 引脚号 (attachInterrupt 用) */

/* 传感器帧 */
#define SENSOR_FRAME_LEN    14
#define SENSOR_HEADER0      0xAA
#define SENSOR_HEADER1      0x55
#define SENSOR_TYPE         0x01
#define SENSOR_PAYLOAD_OFF  2
#define SENSOR_PAYLOAD_LEN  11

/* 接收缓冲 */
#define RX_BUF_LEN          700             /* 最大OTA帧 700B */

/* ========================== 模块变量 ========================== */
static SPI_HandleTypeDef g_spi;
static volatile uint8_t  g_miso_cmd = ESP32_CMD_IDLE;
static uint32_t          g_miso_cmd_set_ms;

/* DMA 环缓冲 (循环模式, 始终运行) */
static uint8_t  g_dma_rx_buf[RX_BUF_LEN];  /* RX DMA 环缓冲 */
static uint8_t  g_dma_tx_buf[RX_BUF_LEN];  /* TX DMA 环缓冲 (MISO 数据) */

/* 环形帧队列 (4槽, ISR入队/任务出队, 防止帧覆盖丢失) */
#define RX_Q_SIZE      4
static uint8_t  rx_q_buf[RX_Q_SIZE][RX_BUF_LEN];
static uint16_t rx_q_len[RX_Q_SIZE];
static volatile uint8_t rx_q_wr;   /* ISR 写入索引 */
static uint8_t          rx_q_rd;   /* 任务读取索引 */

/* SPI 诊断计数 */
static uint16_t rx_frame_total;
static uint16_t rx_frame_ok;       /* CRC校验通过数 */
static uint16_t rx_frame_bad_crc;  /* 帧头找到但CRC失败数 */
static uint16_t rx_frame_bad_type; /* 未知帧类型数 */
static uint8_t  rx_last_type;      /* 最近一次通过的帧类型 (诊断) */

/* 传感器数据 */
static Esp32SensorData g_sensor;
static uint16_t        g_status;

/* OTA 帧回调 (由 ota.cpp 注入) */
esp32_ota_frame_cb_t g_esp32_ota_frame_cb = NULL;

/* ========================== 握手状态机 ========================== */
static Esp32HsState_t g_hs_state = HS_INIT;
static uint32_t       g_hs_state_enter_ms;   /* 进入当前状态的时间 */
static uint8_t        g_hs_boot_status = BOOT_STATUS_NORMAL;
static uint32_t       g_hs_last_heartbeat_ms; /* 上次收到心跳的时间 */

/* MISO 握手响应缓冲区 (9字节循环输出) */
static uint8_t  g_hs_resp_buf[ESP32_HS_RESP_LEN];
static uint8_t  g_hs_resp_idx;
static bool     g_hs_resp_active;

/* ========================== DMA/EXTI 帧检测变量 ========================== */
static volatile uint16_t g_dma_prev_ndtr;   /* 上次 NSS 上升沿的 NDTR */
static volatile bool     g_spi_frame_ready; /* 帧就绪标志 (ISR→任务) */
static volatile bool     g_spi_active;      /* NSS 低电平 = SPI 传输中 */

/* Task handle for notification */
static TaskHandle_t g_esp32_task_handle = NULL;

/* ========================== 前向声明 ========================== */
static void     process_frame(uint8_t *buf, uint16_t len);
static uint8_t  get_miso_byte(void);
static void     hs_build_resp(void);
static void     miso_dma_refill(void);
static void     nss_isr_callback(void);

/* ========================== 辅助 ========================== */
static uint8_t crc8_sum(const uint8_t *data, uint16_t len)
{
    uint16_t sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += data[i];
    return sum & 0xFF;
}

/* ========================== MISO DMA TX 缓冲填充 ========================== */

/** 用当前 MISO 字节模式重新填充 TX DMA 环缓冲 (700字节) */
static void miso_dma_refill(void)
{
    for (uint16_t i = 0; i < RX_BUF_LEN; i++) {
        g_dma_tx_buf[i] = get_miso_byte();
    }
}

/* ========================== 握手状态机 API ========================== */

static void hs_enter_state(Esp32HsState_t new_state)
{
    g_hs_state = new_state;
    g_hs_state_enter_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (new_state == HS_RESPONSE) {
        hs_build_resp();
        g_hs_resp_idx    = 0;
        g_hs_resp_active = true;
    } else {
        g_hs_resp_active = false;
        g_hs_resp_idx    = 0;
    }

    /* MISO 字节模式已改变 → 重新填充 TX DMA 缓冲 */
    miso_dma_refill();
}

/** 构建 MISO 握手响应帧 (9B) */
static void hs_build_resp(void)
{
    uint32_t ver = FIRMWARE_VERSION;
    g_hs_resp_buf[0] = ESP32_MISO_HS_RESP_HDR0;  /* 0xBB */
    g_hs_resp_buf[1] = ESP32_MISO_HS_RESP_HDR1;  /* 0x66 */
    g_hs_resp_buf[2] = (uint8_t)g_hs_state;      /* STM32 握手状态 */
    g_hs_resp_buf[3] = (ver >> 24) & 0xFF;        /* 固件版本 大端 */
    g_hs_resp_buf[4] = (ver >> 16) & 0xFF;
    g_hs_resp_buf[5] = (ver >> 8)  & 0xFF;
    g_hs_resp_buf[6] = (ver)       & 0xFF;
    g_hs_resp_buf[7] = g_hs_boot_status;          /* boot_status */
    g_hs_resp_buf[8] = crc8_sum(g_hs_resp_buf, 8); /* CRC8 */
}

Esp32HsState_t esp32_get_hs_state(void)  { return g_hs_state; }
bool esp32_hs_ready(void)                 { return g_hs_state == HS_READY; }

void esp32_hs_heartbeat_rx(void)
{
    g_hs_last_heartbeat_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint8_t esp32_hs_get_boot_status(void)    { return g_hs_boot_status; }
void    esp32_hs_set_boot_status(uint8_t s) { g_hs_boot_status = s; }

/* ========================== BOOT_REPORT (保留兼容) ========================== */

#define BOOT_REPORT_LEN   10
static uint8_t  g_boot_report[BOOT_REPORT_LEN];
static uint8_t  g_boot_report_idx;
static bool     g_boot_report_active;
static uint32_t g_boot_report_set_ms;

void boot_report_start(uint32_t version, uint8_t boot_status)
{
    g_boot_report[0] = ESP32_CMD_OTA_START;          /* MISO 命令前缀 */
    g_boot_report[1] = ESP32_FRAME_HEADER0;           /* 0xAA */
    g_boot_report[2] = ESP32_FRAME_HEADER1;           /* 0x55 */
    g_boot_report[3] = ESP32_FRAME_BOOT_REPORT;       /* 0x02 */
    g_boot_report[4] = (version >> 24) & 0xFF;
    g_boot_report[5] = (version >> 16) & 0xFF;
    g_boot_report[6] = (version >> 8)  & 0xFF;
    g_boot_report[7] = (version)       & 0xFF;
    g_boot_report[8] = boot_status;
    g_boot_report[9] = crc8_sum(g_boot_report + 1, 8); /* offset 1..8 */
    g_boot_report_idx    = 0;
    g_boot_report_active = true;
    g_boot_report_set_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    miso_dma_refill();  /* MISO 模式已变 */
}

void boot_report_stop(void)
{
    g_boot_report_active = false;
    g_boot_report_idx    = 0;
    miso_dma_refill();  /* MISO 模式已变 */
}

/* ========================== MISO 命令 ========================== */

void esp32_set_miso_cmd(uint8_t cmd)
{
    g_miso_cmd         = cmd;
    g_miso_cmd_set_ms  = xTaskGetTickCount() * portTICK_PERIOD_MS;
    miso_dma_refill();  /* MISO 字节已变 */
}

void esp32_clear_miso_cmd(void)
{
    g_miso_cmd = ESP32_CMD_IDLE;
    miso_dma_refill();
}

/* ========================== MISO 字节选择 ========================== */
/**
 * 根据当前上下文选择 MISO 输出字节:
 *   1. HS_RESPONSE 状态: 循环输出 9B 握手响应帧
 *   2. BOOT_REPORT 激活: 循环输出 10B BOOT_REPORT (向后兼容)
 *   3. 默认: 输出命令字节 g_miso_cmd
 */
static uint8_t get_miso_byte(void)
{
    if (g_hs_resp_active) {
        uint8_t b = g_hs_resp_buf[g_hs_resp_idx];
        g_hs_resp_idx++;
        if (g_hs_resp_idx >= ESP32_HS_RESP_LEN) g_hs_resp_idx = 0;
        return b;
    }

    if (g_boot_report_active) {
        uint8_t b = g_boot_report[g_boot_report_idx];
        g_boot_report_idx++;
        if (g_boot_report_idx >= BOOT_REPORT_LEN) g_boot_report_idx = 0;
        return b;
    }

    return g_miso_cmd;
}

/* ========================== SPI 初始化 (DMA 模式) ========================== */

void esp32_init(void)
{
    /* ---- GPIO 时钟 ---- */
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* ---- GPIO 配置 ---- */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = ESP_SPI_AF;

    gpio.Pin = ESP_PIN_SCK;
    HAL_GPIO_Init(ESP_SPI_PORT, &gpio);

    gpio.Pin = ESP_PIN_MISO;
    HAL_GPIO_Init(ESP_SPI_PORT, &gpio);

    gpio.Pin = ESP_PIN_MOSI;
    HAL_GPIO_Init(ESP_SPI_PORT, &gpio);

    /* NSS 引脚仅用于 EXTI 帧检测, SPI 使用软件 NSS 模式 */
    gpio.Pin  = ESP_PIN_NSS;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ESP_SPI_PORT, &gpio);

    /* ---- SPI2 从机配置 ---- */
    g_spi.Instance            = ESP_SPI;
    g_spi.Init.Mode           = SPI_MODE_SLAVE;
    g_spi.Init.Direction      = SPI_DIRECTION_2LINES;
    g_spi.Init.DataSize       = SPI_DATASIZE_8BIT;
    g_spi.Init.CLKPolarity    = SPI_POLARITY_LOW;   /* CPOL=0 */
    g_spi.Init.CLKPhase       = SPI_PHASE_1EDGE;    /* CPHA=0 */
    g_spi.Init.NSS            = SPI_NSS_SOFT;       /* 软件模式: SPI 始终激活, EXTI 管理帧检测 */
    g_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    g_spi.Init.FirstBit       = SPI_FIRSTBIT_MSB;
    g_spi.Init.TIMode         = SPI_TIMODE_DISABLE;
    g_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    g_spi.Init.CRCPolynomial  = 7;
    HAL_SPI_Init(&g_spi);

    /* NSS_SOFT 模式下 HAL 默认 SSI=1 (从机未选中),
     * 需清除 SSI 使从机始终处于选中状态, 否则不响应 SCK */
    CLEAR_BIT(ESP_SPI->CR1, SPI_CR1_SSI);

    /* ---- 使能 SPI FIFO (4级, 降低 OVR 风险) ---- */
    ESP_SPI->CR2 |= (1UL << 12);  /* CR2 bit12 FRXTH=1 */

    /* ---- 预填 TX DMA 缓冲 (全部填充当前 MISO 字节模式) ---- */
    miso_dma_refill();

    /* ---- 配置 SPI2 RX DMA (DMA1_Stream3, Channel 0) ---- */
    DMA_HandleTypeDef hdma_rx = {0};
    hdma_rx.Instance                 = DMA1_Stream3;
    hdma_rx.Init.Channel             = DMA_CHANNEL_0;      /* SPI2_RX */
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    hdma_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_rx.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_rx.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_rx);
    __HAL_LINKDMA(&g_spi, hdmarx, hdma_rx);

    /* ---- 配置 SPI2 TX DMA (DMA1_Stream4, Channel 0) ---- */
    DMA_HandleTypeDef hdma_tx = {0};
    hdma_tx.Instance                 = DMA1_Stream4;
    hdma_tx.Init.Channel             = DMA_CHANNEL_0;      /* SPI2_TX */
    hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_tx.Init.Mode                = DMA_CIRCULAR;
    hdma_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    hdma_tx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_tx.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_tx.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_tx);
    __HAL_LINKDMA(&g_spi, hdmatx, hdma_tx);

    /* ---- 启动 DMA 传输 (循环模式, 始终运行) ---- */
    HAL_DMA_Start(&hdma_rx, (uint32_t)&ESP_SPI->DR, (uint32_t)g_dma_rx_buf, RX_BUF_LEN);
    HAL_DMA_Start(&hdma_tx, (uint32_t)g_dma_tx_buf, (uint32_t)&ESP_SPI->DR, RX_BUF_LEN);

    /* ---- 使能 SPI DMA 请求 ---- */
    __HAL_SPI_ENABLE(&g_spi);
    ESP_SPI->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

    /* ---- 初始化 NDTR 追踪 ---- */
    g_dma_prev_ndtr    = RX_BUF_LEN;
    g_spi_frame_ready  = false;
    g_spi_active       = false;

    /* ---- NSS (PB12) EXTI: attachInterrupt 双沿触发 ---- */
    attachInterrupt(digitalPinToInterrupt(ESP_PIN_NSS_PIN), nss_isr_callback, CHANGE);

    /* ---- 初始状态 ---- */
    hs_enter_state(HS_INIT);
    g_hs_last_heartbeat_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    memset(&g_sensor, 0, sizeof(g_sensor));
    g_status = 0;
}

/* ========================== NSS EXTI 回调 (attachInterrupt CHANGE) ========================== */

static void nss_isr_callback(void)
{
    if (HAL_GPIO_ReadPin(ESP_SPI_PORT, ESP_PIN_NSS) == GPIO_PIN_RESET) {
        /* 下降沿: 帧开始 (仅记录, 实际位置由 DMA NDTR 追踪) */
    } else {
        /* 上升沿: 帧结束 */
        uint16_t curr_ndtr = (uint16_t)DMA1_Stream3->NDTR;
        uint16_t prev      = g_dma_prev_ndtr;
        g_dma_prev_ndtr    = curr_ndtr;

        /* 计算帧长 */
        uint16_t frame_len;
        if (curr_ndtr == prev) {
            frame_len = RX_BUF_LEN;
        } else if (curr_ndtr < prev) {
            frame_len = prev - curr_ndtr;
        } else {
            frame_len = (RX_BUF_LEN - curr_ndtr) + prev;
        }
        if (frame_len < 10) return;

        /* 写入环形队列槽 (满则覆盖最旧帧) */
        {
            uint8_t slot = rx_q_wr;
            uint8_t next = (slot + 1) % RX_Q_SIZE;
            if (next == rx_q_rd) rx_q_rd = (rx_q_rd + 1) % RX_Q_SIZE;

            uint16_t dma_pos = (RX_BUF_LEN - prev) % RX_BUF_LEN;
            uint8_t *dest = rx_q_buf[slot];
            if (dma_pos + frame_len <= RX_BUF_LEN) {
                memcpy(dest, g_dma_rx_buf + dma_pos, frame_len);
            } else {
                uint16_t part1 = RX_BUF_LEN - dma_pos;
                memcpy(dest, g_dma_rx_buf + dma_pos, part1);
                memcpy(dest + part1, g_dma_rx_buf, frame_len - part1);
            }
            rx_q_len[slot] = frame_len;
            rx_q_wr = next;
        }

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (g_esp32_task_handle)
            vTaskNotifyGiveFromISR(g_esp32_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ========================== 帧处理 ========================== */

static void process_frame(uint8_t *buf, uint16_t len)
{
    if (len < 3) return;

    uint16_t start = 0;
    bool found = false;
    for (uint16_t i = 0; i < len - 1; i++) {
        if (buf[i] == 0xAA && buf[i + 1] == 0x55) { start = i; found = true; break; }
    }
    if (!found) { rx_frame_total++; return; }

    uint16_t effective_len = len - start;
    if (start > 0) memmove(buf, buf + start, effective_len);

    uint8_t type = buf[2];

    /* 确定此帧类型的 CRC 覆盖载荷长度 */
    uint16_t crc_payload;
    uint8_t  min_len;

    switch (type) {
    case ESP32_FRAME_HANDSHAKE_REQ:
    case ESP32_FRAME_HANDSHAKE_ACK:
    case ESP32_FRAME_HEARTBEAT:
        crc_payload = 2;
        min_len     = 5;
        break;

    case ESP32_FRAME_SENSOR:
        crc_payload = 11;
        min_len     = 14;
        break;

    case ESP32_FRAME_OTA_HANDSHAKE:
        crc_payload = 41;
        min_len     = 44;
        break;

    case ESP32_FRAME_OTA_RESULT:
        crc_payload = 3;
        min_len     = 6;
        break;

    case ESP32_FRAME_OTA_DATA:
        if (effective_len < 10) return;
        {
            uint16_t chunk = ((uint16_t)buf[7] << 8) | buf[8];
            if (chunk > 512) return;
            crc_payload = 7 + chunk;
            min_len     = 10 + chunk;
        }
        break;

    default:
        rx_frame_total++;
        rx_frame_bad_type++;
        return;
    }

    if (effective_len < min_len) {
        rx_frame_total++;
        return;
    }

    uint8_t crc_exp = crc8_sum(buf + 2, crc_payload);
    uint8_t crc_rx  = buf[2 + crc_payload];

    if (crc_rx != crc_exp) {
        rx_frame_total++;
        rx_frame_bad_crc++;
        return;
    }

    rx_frame_total++;
    rx_frame_ok++;
    rx_last_type = type;  /* 记录最近通过的帧类型 */

    switch (type) {
    case ESP32_FRAME_HANDSHAKE_REQ:
        if (g_hs_state == HS_INIT || g_hs_state == HS_READY) {
            hs_enter_state(HS_RESPONSE);
        }
        break;

    case ESP32_FRAME_HANDSHAKE_ACK:
        if (g_hs_state == HS_RESPONSE) {
            hs_enter_state(HS_READY);
            if (g_esp32_ota_frame_cb) {
                g_esp32_ota_frame_cb(type, buf, effective_len);
            }
        }
        if (g_hs_state == HS_READY) {
            esp32_hs_heartbeat_rx();
        }
        break;

    case ESP32_FRAME_HEARTBEAT:
        esp32_hs_heartbeat_rx();
        break;

    case ESP32_FRAME_SENSOR:
        {
            if (effective_len >= SENSOR_FRAME_LEN) {
                buf[SENSOR_FRAME_LEN] = 0;
                g_sensor.temp_x100 = ((int16_t)buf[3]  << 8) | buf[4];
                g_sensor.humi_x100 = ((int16_t)buf[5]  << 8) | buf[6];
                g_sensor.co2       = ((int16_t)buf[7]  << 8) | buf[8];
                g_sensor.nh3_x100  = ((int16_t)buf[9]  << 8) | buf[10];
                g_sensor.lux_x100  = ((int16_t)buf[11] << 8) | buf[12];
                g_sensor.valid     = true;
                g_sensor.update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                g_status |= 0x02;
            }
        }
        break;

    case ESP32_FRAME_OTA_HANDSHAKE:
    case ESP32_FRAME_OTA_DATA:
    case ESP32_FRAME_OTA_RESULT:
        if (g_hs_state == HS_READY) {
            if (g_esp32_ota_frame_cb) {
                g_esp32_ota_frame_cb(type, buf, effective_len);
            }
        }
        break;

    default:
        break;
    }
}

/* ========================== 查询接口 ========================== */

uint16_t esp32_get_status(void) { return g_status; }

void esp32_get_data(Esp32SensorData *out)
{
    if (out) {
        *out = g_sensor;
    }
}

/* ========================== FreeRTOS 任务 ========================== */

void esp32_task(void *pvParameters)
{
    (void)pvParameters;

    g_esp32_task_handle = xTaskGetCurrentTaskHandle();
    vTaskDelay(pdMS_TO_TICKS(50));

    for (;;) {
        /* 每循环修复 GPIOB: LCD 初始化会用 GPIOB->MODER = xxx 全量覆盖 */
        {
            GPIO_InitTypeDef fix = {0};
            fix.Mode      = GPIO_MODE_AF_PP;
            fix.Pull      = GPIO_NOPULL;
            fix.Speed     = GPIO_SPEED_FREQ_HIGH;
            fix.Alternate = ESP_SPI_AF;
            fix.Pin = ESP_PIN_SCK;   HAL_GPIO_Init(ESP_SPI_PORT, &fix);
            fix.Pin = ESP_PIN_MISO;  HAL_GPIO_Init(ESP_SPI_PORT, &fix);
            fix.Pin = ESP_PIN_MOSI;  HAL_GPIO_Init(ESP_SPI_PORT, &fix);
            fix.Mode      = GPIO_MODE_INPUT;
            fix.Pull      = GPIO_PULLUP;
            fix.Pin = ESP_PIN_NSS;   HAL_GPIO_Init(ESP_SPI_PORT, &fix);
        }

        /* 等待 SPI 帧通知 (100ms 超时用于状态机维护) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        /* 排空环形帧队列 (ISR 入队, 任务出队, 无锁安全) */
        while (rx_q_rd != rx_q_wr) {
            process_frame(rx_q_buf[rx_q_rd], rx_q_len[rx_q_rd]);
            rx_q_rd = (rx_q_rd + 1) % RX_Q_SIZE;
        }

        /* 握手状态机超时检测 */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (g_hs_state) {
        case HS_RESPONSE:
            if (now - g_hs_state_enter_ms >= HS_ACK_TIMEOUT_MS) {
                hs_enter_state(HS_INIT);
            }
            break;

        case HS_READY:
            if (now - g_hs_last_heartbeat_ms >= HS_HEARTBEAT_TIMEOUT_MS) {
                g_status &= ~0x02;
                hs_enter_state(HS_INIT);
            }
            break;

        default:
            break;
        }

        /* MISO 命令自动清除 (5s) */
        if (g_miso_cmd != ESP32_CMD_IDLE && g_hs_state == HS_READY) {
            if (now - g_miso_cmd_set_ms >= 5000) {
                g_miso_cmd = ESP32_CMD_IDLE;
                miso_dma_refill();
            }
        }

        /* BOOT_REPORT 超时停用 (15s) */
        if (g_boot_report_active) {
            if (now - g_boot_report_set_ms >= 15000) {
                g_boot_report_active = false;
                miso_dma_refill();
            }
        }

        /* SPI 诊断 (每10秒) */
        {
            static uint32_t last_diag = 0;
            if (now - last_diag >= 10000) {
                last_diag = now;
                DBG_FMT("SPI.DIAG", "total=%u ok=%u bad_crc=%u type=%02X hs=%d cmd=%02X",
                        rx_frame_total, rx_frame_ok, rx_frame_bad_crc,
                        rx_last_type, (int)g_hs_state, g_miso_cmd);
            }
        }
    }
}
