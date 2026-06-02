#ifndef _my_LORA_TASK_H_
#define _my_LORA_TASK_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <RadioLib.h>
#include "IO_Setting.h"
#include "myShowMsg.h"
#include "myModbus.h"
#include "myMQTT_TLS.h"

/* LoRa数据帧长度 */
#define LORA_FRAME_LEN 3

/* 数据类型标志定义 */
#define LORA_FLAG_TEMP  0x0A
#define LORA_FLAG_HUMID 0x0B
#define LORA_FLAG_CO2   0x0C
#define LORA_FLAG_NH3   0x0D

/* LoRa数据Modbus寄存器地址 - 与本地传感器数据分隔开
 * 本地传感器数据寄存器: 10-18 (时间、输入、输出、AI0-AI3)
 * LoRa机器人采集数据寄存器: 20-23
 */
#define LORA_MODBUS_REG_TEMP  20  // LoRa温度 (放大10倍)
#define LORA_MODBUS_REG_HUMID 21  // LoRa湿度 (放大10倍)
#define LORA_MODBUS_REG_CO2   22  // LoRa CO2浓度
#define LORA_MODBUS_REG_NH3   23  // LoRa NH3浓度 (放大100倍)

/* SX1262 引脚定义 - 使用IO_Setting.h中定义的引脚 */
// NSS    -> PB12
// DIO1   -> PD2 (已修改，避免与RUN_LED/PC2冲突)
// RESET  -> PC0
// BUSY   -> PC1

/* 创建SX1262模块实例 */
extern SX1262 radio;

/* 接收缓冲区 */
extern uint8_t loraRxBuffer[LORA_FRAME_LEN];
extern volatile uint8_t loraRxIndex;

/* LoRa接收任务句柄 - 用于中断中通知任务 */
extern TaskHandle_t xLoRaTaskHandle;

/* 串口屏串口实例 - 使用mySensorTask.h中定义的HMISerial */
extern HardwareSerial HMISerial;

/* ModbusRTU实例 - 用于写入LoRa数据到寄存器 */
extern ModbusSerial myModbusRTU;

/* 函数声明 */
void LoRa_DIO1_ISR(void);
void SendToHMI_LoRa(uint8_t flag, float value, uint8_t decimalPlaces);
void ParseLoRaFrame(uint8_t *buffer, size_t len);
int16_t Ra01S_Init(void);
int16_t Ra01S_StartReceive(void);
void LoRaReceiveTask(void *pvParameters);

#endif
