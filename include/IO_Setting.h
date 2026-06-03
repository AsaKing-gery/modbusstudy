#ifndef _IO_SETTING_H_
#define _IO_SETTING_H_
#include <Arduino.h>
#include <SPI.h>
#include "Parameter_Config.h"
#include "myShowMsg.h"

extern uint8_t idSwitchState;       // 站号拨码开关状态
extern uint8_t baudRateSwitchState; // 波特率拨码开关状态

extern SPIClass RA01S_SPI; // SPI2: MOSI, MISO, SCK

/*********通信引脚*********/

#define mbRxPin PB11    // 接收引脚
#define mbTxPin PB10    // 发送引脚
#define mbSendEnPin PB1 // 发送使能引脚

/*********K210摄像头模块引脚定义*********/
#define K210_USART_RX PC7 // USART6 RX
#define K210_USART_TX PC6 // USART6 TX

/*********ESP32C6Mini模块引脚定义*********/
#define ESP32C6_SPI_SCK PC10  // SPI3_SCK
#define ESP32C6_SPI_MOSI PC12 // SPI3_MOSI
#define ESP32C6_SPI_MISO PC11 // SPI3_MISO
#define ESP32C6_SPI_CS PD3    // CS
#define ESP32C6_EN PE0        // EN

/*********Ra-01S模块引脚定义*********/
#define RA01S_RESET PC0  // RESET
#define RA01S_DIO1 PD2   // DIO1 - 修改为PD2，避免与RUN_LED(PC2)冲突
#define RA01S_SPI_NSS PB12 // SPI2_NSS
#define RA01S_SPI_MOSI PB15 // SPI2_MOSI
#define RA01S_SPI_MISO PB14 // SPI2_MISO
#define RA01S_SPI_SCK PB13 // SPI2_SCK
#define RA01S_BUSY PC1   // BUSY

/*********串口屏引脚定义*********/
#define HMI_USART_RX PA1 // USART4 RX
#define HMI_USART_TX PA0 // USART4 TX

/*********拨码开关引脚定义*********/
/**
 * SW_B1 SW_B2 SW_B3组合表示站号
 * SW_B4 SW_B5 组合表示波特率
 */
#define SW_B1 PC15
#define SW_B2 PE3
#define SW_B3 PE2
#define SW_B4 PE1
#define SW_B5 PC3

/*********输入引脚定义*********/
#define Temp_X0 PD10
#define Temp_X1 PD11
#define Temp_X2 PD12
#define Temp_X3 PD13
#define Temp_X4 PA8
#define Temp_X5 PB0
#define Temp_X6 PE6
#define Temp_X7 PE4

/// @brief GPIO端口定义
typedef struct
{
    uint8_t X0 : 1;
    uint8_t X1 : 1;
    uint8_t X2 : 1;
    uint8_t X3 : 1;
    uint8_t X4 : 1;
    uint8_t X5 : 1;
    uint8_t X6 : 1;
    uint8_t X7 : 1;
} GPIO_Port;

/// @brief 输入端口
extern GPIO_Port Input;

/*********输出引脚定义*********/
#define Output_Y0 PA15
#define Output_Y1 PB3
#define Output_Y2 PB4
#define Output_Y3 PB5
#define Output_Y4 PB6
#define Output_Y5 PB7
#define Output_Y6 PE10
#define Output_Y7 PE11
#define Output_Y8 PE12
#define Output_Y9 PE13

/*********指示灯定义*********/
#define ERROR_LED PC13
#define RUN_LED PC2
#define BOARD_LED PB2 // 板载运行指示灯，高电平点亮

/*********模拟量定义*********/
typedef struct
{
    int16_t AI0;
    int16_t AI1;
    int16_t AI2;
    int16_t AI3;
} AnalogStruct;

extern AnalogStruct myAI;


/*设置输出模式并设置为低电平*/
void pinMode_OutSetting(uint32_t ulPin);

/**
 * GPIO初始化
 */
void GPIO_Init();

/*输入滤波函数*/
void X_filter(void *pvParameters); // 每1MS调用一次，用来给输入滤波,滤波时间由Input_Filter_Time指定，默认5ms

#endif