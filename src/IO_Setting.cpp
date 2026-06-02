#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myShowMsg.h"

void pinMode_OutSetting(uint32_t ulPin)
{
    pinMode(ulPin, OUTPUT_OPEN_DRAIN);
    digitalWrite(ulPin, HIGH);
}

void GPIO_Init()
{
    ShowMsg("GPIO_Initizing", true);
    pinMode(SW_B1, INPUT_PULLUP);
    pinMode(SW_B2, INPUT_PULLUP);
    pinMode(SW_B3, INPUT_PULLUP);
    pinMode(SW_B4, INPUT_PULLUP);
    pinMode(SW_B5, INPUT_PULLUP);
    pinMode(Temp_X0, INPUT);
    pinMode(Temp_X1, INPUT);
    pinMode(Temp_X2, INPUT);
    pinMode(Temp_X3, INPUT);
    pinMode(Temp_X4, INPUT);
    pinMode(Temp_X5, INPUT);
    pinMode(Temp_X6, INPUT);
    pinMode(Temp_X7, INPUT);
    pinMode_OutSetting(Output_Y0);
    pinMode_OutSetting(Output_Y1);
    pinMode_OutSetting(Output_Y2);
    pinMode_OutSetting(Output_Y3);
    pinMode_OutSetting(Output_Y4);
    pinMode_OutSetting(Output_Y5);
    pinMode_OutSetting(Output_Y6);
    pinMode_OutSetting(Output_Y7);
    pinMode_OutSetting(Output_Y8);
    pinMode_OutSetting(Output_Y9);
    pinMode_OutSetting(ERROR_LED);
    pinMode_OutSetting(RUN_LED);
    pinMode(HMI_USART_RX, INPUT);
    pinMode(HMI_USART_TX, OUTPUT);
    digitalWrite(HMI_USART_TX, HIGH);
    pinMode(RA01S_RESET, OUTPUT);
    digitalWrite(RA01S_RESET, HIGH);
    pinMode(RA01S_DIO1, INPUT_PULLUP);
    pinMode(RA01S_SPI_NSS, OUTPUT);
    digitalWrite(RA01S_SPI_NSS, HIGH);
    pinMode(RA01S_BUSY, INPUT);
    RA01S_SPI.begin();
    pinMode(K210_USART_RX, INPUT_PULLUP);
    pinMode(K210_USART_TX, OUTPUT);
    digitalWrite(K210_USART_TX, HIGH);
    idSwitchState = (!digitalRead(SW_B3) << 2) | (!digitalRead(SW_B2) << 1) | !digitalRead(SW_B1);
    baudRateSwitchState = (!digitalRead(SW_B5) << 1) | !digitalRead(SW_B4);
    myPar.SlaveId = (idSwitchState == 0 ? 1 : idSwitchState);
    switch (baudRateSwitchState)
    {
    case 0:
        myPar.Baudrate = 115200;
        break;
    case 1:
        myPar.Baudrate = 9600;
        break;
    case 2:
        myPar.Baudrate = 19200;
        break;
    case 3:
        myPar.Baudrate = 38400;
        break;
    default:
        myPar.Baudrate = 115200;
        break;
    }
    ShowMsg("GPIO_Initized", true);
}

void X_filter(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ShowMsg("X_filter task started", true);
    static uint8_t x_buffer[8];
    static uint32_t timeRecord = 0;
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (millis() - timeRecord >= 1)
        {
            timeRecord = millis();
            (digitalRead(Temp_X0)) ? (x_buffer[0] = 0, Input.X0 = 0) : ((x_buffer[0] < myPar.Input_Filter_Time) ? (x_buffer[0]++) : (Input.X0 = 1));
            (digitalRead(Temp_X1)) ? (x_buffer[1] = 0, Input.X1 = 0) : ((x_buffer[1] < myPar.Input_Filter_Time) ? (x_buffer[1]++) : (Input.X1 = 1));
            (digitalRead(Temp_X2)) ? (x_buffer[2] = 0, Input.X2 = 0) : ((x_buffer[2] < myPar.Input_Filter_Time) ? (x_buffer[2]++) : (Input.X2 = 1));
            (digitalRead(Temp_X3)) ? (x_buffer[3] = 0, Input.X3 = 0) : ((x_buffer[3] < myPar.Input_Filter_Time) ? (x_buffer[3]++) : (Input.X3 = 1));
            (digitalRead(Temp_X4)) ? (x_buffer[4] = 0, Input.X4 = 0) : ((x_buffer[4] < myPar.Input_Filter_Time) ? (x_buffer[4]++) : (Input.X4 = 1));
            (digitalRead(Temp_X5)) ? (x_buffer[5] = 0, Input.X5 = 0) : ((x_buffer[5] < myPar.Input_Filter_Time) ? (x_buffer[5]++) : (Input.X5 = 1));
            (digitalRead(Temp_X6)) ? (x_buffer[6] = 0, Input.X6 = 0) : ((x_buffer[6] < myPar.Input_Filter_Time) ? (x_buffer[6]++) : (Input.X6 = 1));
            (digitalRead(Temp_X7)) ? (x_buffer[7] = 0, Input.X7 = 0) : ((x_buffer[7] < myPar.Input_Filter_Time) ? (x_buffer[7]++) : (Input.X7 = 1));
        }
    }
}
