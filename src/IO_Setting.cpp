#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myShowMsg.h"

void pinMode_OutSetting(uint32_t ulPin)
{
    pinMode(ulPin, OUTPUT);
    digitalWrite(ulPin, HIGH);
}

void GPIO_Init()
{
    Serial.println("G"); Serial.flush();
    pinMode_OutSetting(Output_Y1);
    Serial.println("G1"); Serial.flush();
    pinMode_OutSetting(Output_Y2);
    pinMode_OutSetting(Output_Y3);
    pinMode_OutSetting(Output_Y4);
    Serial.println("G2"); Serial.flush();
    pinMode_OutSetting(Output_Y5);
    pinMode_OutSetting(Output_Y6);
    pinMode_OutSetting(Output_Y7);
    pinMode_OutSetting(Output_Y8);
    Serial.println("G3"); Serial.flush();
    pinMode_OutSetting(ERROR_LED);
    pinMode_OutSetting(RUN_LED);
    Serial.println("G4"); Serial.flush();
    pinMode(HMI_USART_RX, INPUT);
    pinMode(HMI_USART_TX, OUTPUT);
    digitalWrite(HMI_USART_TX, HIGH);
    Serial.println("G5"); Serial.flush();
    // K210 暂不使用，跳过引脚初始化 (PD5/PD6为调试串口)
    Serial.println("G6"); Serial.flush();

    /* 上电初始化：8路继电器全部断开 */
    digitalWrite(Output_Y1, HIGH);
    digitalWrite(Output_Y2, HIGH);
    digitalWrite(Output_Y3, HIGH);
    digitalWrite(Output_Y4, HIGH);
    digitalWrite(Output_Y5, HIGH);
    digitalWrite(Output_Y6, HIGH);
    digitalWrite(Output_Y7, HIGH);
    digitalWrite(Output_Y8, HIGH);
    Serial.println("G7"); Serial.flush();

    Serial.println("g"); Serial.flush();
}
