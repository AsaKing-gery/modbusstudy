/******************************************************************************
 * STM32F407 SPI3 Slave HAL 参考代码
 * 适配接线：PC10(SCK), PC11(MISO), PC12(MOSI), PD3(CS/GPIO)
 * 配合 ESP32 Master (SPI_MODE0, 1MHz, MSB First)
 *
 * 使用方式：将以下代码片段复制到你的 STM32CubeMX 生成的工程中使用。
 ******************************************************************************/

#include "stm32f4xx_hal.h"
#include <string.h>

/* ===================== 用户可调整参数 ===================== */
#define SPI_SLAVE_BUF_SIZE  64   // SPI 收发缓冲区大小

/* ===================== 全局变量 ===================== */
SPI_HandleTypeDef hspi3;

// 双缓冲：一组用于当前传输，一组用于处理
static uint8_t spiTxBuf[SPI_SLAVE_BUF_SIZE];
static uint8_t spiRxBuf[SPI_SLAVE_BUF_SIZE];
static volatile uint8_t spiRxReady = 0;   // 接收完成标志
static volatile uint8_t spiCsActive = 0;  // CS 低电平标志（可选）

/* ===================== GPIO / AF 初始化 ===================== */
/**
 * @brief 初始化 SPI3 所用 GPIO：PC10(SCK), PC11(MISO), PC12(MOSI), PD3(CS)
 *        如果你使用 CubeMX，这一步可以省略，由 MX_GPIO_Init 完成。
 */
void MX_SPI3_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能 GPIOC、GPIOD 时钟
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // PC10(SCK), PC12(MOSI) -> AF6, 输入
    GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_12;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // PC11(MISO) -> AF6, 推挽输出
    GPIO_InitStruct.Pin       = GPIO_PIN_11;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;  // Slave 的 MISO 是输出
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // PD3(CS) -> GPIO 输入，下拉（假设 CS 低有效）
    GPIO_InitStruct.Pin       = GPIO_PIN_3;
    GPIO_InitStruct.Mode      = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull      = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

/* ===================== SPI3 初始化 ===================== */
void MX_SPI3_Init(void)
{
    hspi3.Instance = SPI3;
    hspi3.Init.Mode              = SPI_MODE_SLAVE;          // 从机模式
    hspi3.Init.Direction         = SPI_DIRECTION_2LINES;    // 全双工
    hspi3.Init.DataSize          = SPI_DATASIZE_8BIT;       // 8 位数据
    hspi3.Init.CLKPolarity       = SPI_POLARITY_LOW;        // CPOL = 0 (MODE0)
    hspi3.Init.CLKPhase          = SPI_PHASE_1EDGE;         // CPHA = 0 (MODE0)
    hspi3.Init.NSS               = SPI_NSS_SOFT;            // 软件 NSS
    hspi3.Init.FirstBit          = SPI_FIRSTBIT_MSB;        // MSB 先传
    hspi3.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi3.Init.CRCPolynomial     = 10;
    // BaudRatePrescaler 在 Slave 模式下无实际意义，时钟由 Master 提供
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;

    if (HAL_SPI_Init(&hspi3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ===================== NVIC 中断配置 ===================== */
void MX_SPI3_NVIC_Init(void)
{
    // SPI3 全局中断
    HAL_NVIC_SetPriority(SPI3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SPI3_IRQn);
}

/* ===================== 启动 SPI 中断接收 ===================== */
/**
 * @brief 启动一次中断方式的 SPI 收发（全双工）
 *        Slave 收到 Master 发来的时钟后自动交换数据。
 */
void SPI3_Slave_Start_IT(void)
{
    // 预填充发送缓冲区（Master 读到的就是这些数据）
    // 示例：把接收到的内容 +1 后回传，或固定回传某个标志
    memset(spiTxBuf, 0x00, SPI_SLAVE_BUF_SIZE);
    spiTxBuf[0] = 0xBB;  // 首字节作为回应标志

    HAL_SPI_TransmitReceive_IT(&hspi3, spiTxBuf, spiRxBuf, SPI_SLAVE_BUF_SIZE);
}

/* ===================== SPI 中断服务函数 ===================== */
void SPI3_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi3);
}

/* ===================== HAL 回调函数 ===================== */
/**
 * @brief SPI 传输完成回调
 *        当 Master 发来足够的 SCK 时钟，交换完成后触发
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI3)
    {
        spiRxReady = 1;
        // 可在此重新启动下一次传输，实现连续收发
        // SPI3_Slave_Start_IT();
    }
}

/**
 * @brief SPI 错误回调
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI3)
    {
        // 出现溢出(OVR)或帧错误(FRE)等
        // 通常 Slave 出现 OVR 是因为 Master 发送太快，Slave 来不及读取 DR
        __HAL_SPI_CLEAR_OVRFLAG(hspi);
        __HAL_SPI_CLEAR_FREFLAG(hspi);

        // 重新启动接收
        SPI3_Slave_Start_IT();
    }
}

/* ===================== 供 main.c 使用的封装 ===================== */
/**
 * @brief 初始化并启动 SPI3 Slave
 */
void SPI3_Slave_Init(void)
{
    MX_SPI3_GPIO_Init();
    MX_SPI3_Init();
    MX_SPI3_NVIC_Init();
    SPI3_Slave_Start_IT();
}

/**
 * @brief 主循环中调用，检查是否收到新数据
 * @return 1=有新数据，0=无
 */
uint8_t SPI3_Slave_CheckRx(void)
{
    if (spiRxReady)
    {
        spiRxReady = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief 获取接收缓冲区指针（读取后尽快处理，会被下一次传输覆盖）
 */
uint8_t* SPI3_Slave_GetRxBuf(void)
{
    return (uint8_t*)spiRxBuf;
}

/**
 * @brief 设置发送缓冲区内容（下次 Master 通信时发出）
 */
void SPI3_Slave_SetTxBuf(const uint8_t *data, uint16_t len)
{
    if (len > SPI_SLAVE_BUF_SIZE) len = SPI_SLAVE_BUF_SIZE;
    memcpy(spiTxBuf, data, len);
}

/* ===================== 主循环参考 (main.c) ===================== */
#if 0  // 以下片段复制到你的 main.c 的 while(1) 中

    if (SPI3_Slave_CheckRx())
    {
        uint8_t *rx = SPI3_Slave_GetRxBuf();

        // 示例：把收到的数据打印（通过 UART 或 SWO）
        printf("[SPI Slave] RX: ");
        for (int i = 0; i < 8; i++) {
            printf("0x%02X ", rx[i]);
        }
        printf("\n");

        // 准备回传数据（ESP32 会在同一次传输中读到）
        uint8_t tx[8] = {0xBB, 0xCC, rx[2]+1, rx[3]+1, 0, 0, 0, 0};
        SPI3_Slave_SetTxBuf(tx, 8);

        // 重新启动下一次中断接收
        SPI3_Slave_Start_IT();
    }

#endif

/******************************************************************************
 * 注意事项：
 * 1. 本代码假设使用 HAL 库。若使用标准库(LL/SPL)，需要自行改写寄存器操作。
 * 2. SPI Slave 容易出现 OVR（溢出），因为 Master 发时钟时 Slave 必须及时
 *    从 DR 读取数据。使用中断或 DMA 可减少 OVR 概率。
 * 3. 如果只想用轮询方式（简单但不高效），可用 HAL_SPI_TransmitReceive()
 *    在 CS 低电平时阻塞等待 Master 时钟。但不推荐。
 * 4. 如果 Master 每次传输的字节数不固定，建议采用“先传长度再传数据”
 *    的协议，或使用固定帧长 + 头标志位的方式。
 ******************************************************************************/
