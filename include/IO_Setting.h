#ifndef _IO_SETTING_H_
#define _IO_SETTING_H_
#include <Arduino.h>
#include <SPI.h>
#include "Parameter_Config.h"
#include "myShowMsg.h"

/*********通信引脚 (RS485 Modbus - USART1)*********/

#define mbRxPin PA10   // 接收引脚, USART1_RX
#define mbTxPin PA9    // 发送引脚, USART1_TX
#define mbSendEnPin PC4 // 发送使能引脚, 485_EN

/*********K210摄像头模块引脚定义 (USART2, 暂未使用)*********/
#define K210_USART_RX PD6 // USART2 RX
#define K210_USART_TX PD5 // USART2 TX

/*********ESP32C6Mini模块引脚定义 (SPI2)*********/
#define ESP32C6_SPI_SCK PB13  // SPI2_SCK
#define ESP32C6_SPI_MOSI PB15 // SPI2_MOSI
#define ESP32C6_SPI_MISO PB14 // SPI2_MISO
#define ESP32C6_SPI_CS PB12   // SPI2_NSS, 硬件片选

/*********4G模块引脚定义 (USART6)*********/
#define G4G_UART_TX PA7  // USART6 TX
#define G4G_UART_RX PA6  // USART6 RX
#define G4G_RDY PE6      // 模块就绪状态
#define G4G_DTR PE5      // 数据终端就绪
#define G4G_RST PC11     // 模块复位

/*********LCD SPI屏幕引脚定义 (SPI1)*********/
#define LCD_BLK PE4  // 背光, 可PWM调光
#define LCD_CS PE3   // 片选
#define LCD_DC PE2   // 数据/命令选择
#define LCD_SDI PB5  // SPI1 MOSI, 只写无需MISO
#define LCD_SCK PB3  // SPI1 SCK, 重映射

/*********串口屏引脚定义 (UART7)*********/
#define HMI_USART_RX PC10 // UART7 RX
#define HMI_USART_TX PA15 // UART7 TX

/*********输出引脚定义 (8路继电器 PE7~PE14)*********/
/* Y1~Y4: 加湿器1~4 | Y5~Y8: 风机1~4 */
#define Output_Y1 PE7   // 加湿器1
#define Output_Y2 PE8   // 加湿器2
#define Output_Y3 PE9   // 加湿器3
#define Output_Y4 PE10  // 加湿器4
#define Output_Y5 PE11  // 风机1
#define Output_Y6 PE12  // 风机2
#define Output_Y7 PE13  // 风机3
#define Output_Y8 PE14  // 风机4

/*********指示灯定义*********/
#define ERROR_LED PE1 // 高电平点亮
#define RUN_LED PE0   // 高电平点亮

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

#endif