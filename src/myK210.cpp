#include <Arduino.h>
#include "myK210.h"
#include "myShowMsg.h"

void K210_Initialize(uint32_t baudrate)
{
    ShowMsg("K210 initializing", true);

    /* 新板: K210使用USART2, PD5(TX)/PD6(RX) */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    k210Serial.begin(baudrate);

    ShowMsg("K210 initialized", true);
}

void K210_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ShowMsg("K210 task started", true);

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
