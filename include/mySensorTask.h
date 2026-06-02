#ifndef _my_Sensor_Task_H_
#define _my_Sensor_Task_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "IO_Setting.h"
#include "myShowMsg.h"
#include "myModbus.h"

/* 使用板子原有的RS485串口 - 与ModbusRTU共用mbSerial (PB10/PB11) 和 PB1方向控制 */
#define SENSOR_RS485_EN mbSendEnPin // RS485方向控制引脚，使用原有定义

/* 串口屏USART4引脚已在IO_Setting.h中定义 */
// #define HMI_USART_RX PA1 // USART4 RX
// #define HMI_USART_TX PA0 // USART4 TX

/* 传感器Modbus参数 */
#define SENSOR_SLAVE_ID     0x01
#define SENSOR_FUNC_CODE    0x03
#define SENSOR_BAUDRATE     9600

/* 寄存器地址 */
#define REG_TEMP_HUMID_START 0x0000
#define REG_TEMP_HUMID_COUNT 0x0002

#define REG_CO2_START        0x0005
#define REG_CO2_COUNT        0x0001

#define REG_NH3_START        0x0009
#define REG_NH3_COUNT        0x0001

/* 响应帧长度 */
#define RESP_TEMP_HUMID_LEN  9  // 01 03 04 HH HL TH TL CRCL CRCH
#define RESP_CO2_LEN         7  // 01 03 02 CH CL CRCL CRCH
#define RESP_NH3_LEN         7  // 01 03 02 NH NL CRCL CRCH

/* 串口屏数据类型标志 */
#define HMI_FLAG_TEMP        0x0A
#define HMI_FLAG_HUMID       0x0B
#define HMI_FLAG_CO2         0x0C
#define HMI_FLAG_NH3         0x0D
#define HMI_FLAG_FAN_CTRL    0x0E // 风机控制命令
#define HMI_FLAG_HUMI_CTRL   0x0F // 加湿器控制命令

/* 传感器Modbus寄存器地址定义 (使用25-29，避开LoRa的20-23和AI0-AI3的15-18) */
#define REG_SENSOR_TEMP       25 // 温度值 (放大10倍，如256表示25.6度)
#define REG_SENSOR_HUMID      26 // 湿度值 (放大10倍)
#define REG_SENSOR_CO2        27 // CO2浓度 (ppm，原始值)
#define REG_SENSOR_NH3        28 // NH3浓度 (放大100倍，如123表示1.23ppm)
#define REG_SENSOR_STATUS     29 // 传感器状态位: bit0=温湿度有效, bit1=CO2有效, bit2=NH3有效

/* 风机控制命令码 */
#define FAN1_OPEN  0x01
#define FAN2_OPEN  0x02
#define FAN3_OPEN  0x03
#define FAN4_OPEN  0x04
#define FAN1_CLOSE 0x11
#define FAN2_CLOSE 0x12
#define FAN3_CLOSE 0x13
#define FAN4_CLOSE 0x14

/* 风机对应输出引脚 */
#define FAN1_PIN Output_Y2 // 风机1 -> Y2
#define FAN2_PIN Output_Y3 // 风机2 -> Y3
#define FAN3_PIN Output_Y4 // 风机3 -> Y4
#define FAN4_PIN Output_Y5 // 风机4 -> Y5

/* 加湿器控制命令码 */
#define HUMI1_OPEN  0x01
#define HUMI2_OPEN  0x02
#define HUMI3_OPEN  0x03
#define HUMI4_OPEN  0x04
#define HUMI1_CLOSE 0x11
#define HUMI2_CLOSE 0x12
#define HUMI3_CLOSE 0x13
#define HUMI4_CLOSE 0x14

/* 加湿器对应输出引脚 */
#define HUMI1_PIN Output_Y6 // 加湿器1 -> Y6
#define HUMI2_PIN Output_Y7 // 加湿器2 -> Y7
#define HUMI3_PIN Output_Y8 // 加湿器3 -> Y8
#define HUMI4_PIN Output_Y9 // 加湿器4 -> Y9

/* 传感器轮询周期 - 5秒 */
#define SENSOR_POLL_INTERVAL_MS 5000

/* 传感器互斥锁 - 三个传感器任务和ModbusRTU任务共用RS485总线 */
extern SemaphoreHandle_t xSensorMutex;

/* 设备状态结构体和互斥锁 - 定义在myDeviceState.h中，避免循环依赖 */
#include "myDeviceState.h"

/* 传感器串口使用板子原有的mbSerial (PB10/PB11)，不再新建HardwareSerial实例 */

/* 串口屏串口实例 */
extern HardwareSerial HMISerial;

/* 函数声明 */
uint16_t ModbusCRC16(uint8_t *data, uint8_t length);
uint8_t BuildModbusFrame(uint8_t *frame, uint8_t slaveId, uint8_t funcCode, uint16_t regStart, uint16_t regCount);
void SendSensorFrame(uint8_t *frame, uint8_t len);
uint8_t ReceiveSensorResponse(uint8_t *buffer, uint8_t expectedLen, uint32_t timeoutMs);
bool VerifyResponseCRC(uint8_t *buffer, uint8_t len);
void SendToHMI(uint8_t flag, float value, uint8_t decimalPlaces);
void SensorSerial_Init();
void ProcessFanControl(uint8_t cmd);
void ProcessHumiControl(uint8_t cmd);
void HMIReceiveTask(void *pvParameters);

/************************************************************************************
 * 传感器状态机定义
 ************************************************************************************/
enum SensorState
{
    STATE_READ_TEMP_HUMID = 0,  // 读取温湿度
    STATE_READ_CO2,             // 读取CO2
    STATE_READ_NH3,             // 读取NH3
    STATE_SEND_HMI,             // 发送数据到串口屏
    STATE_WAIT_INTERVAL         // 等待轮询间隔
};

void SensorStateMachineTask(void *pvParameters);

#endif
