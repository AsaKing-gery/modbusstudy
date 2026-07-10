#ifndef _my_Sensor_Task_H_
#define _my_Sensor_Task_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <SoftwareSerial.h>
#include "IO_Setting.h"
#include "myShowMsg.h"
#include "myModbus.h"

/* 使用板子原有的RS485串口 - 与ModbusRTU共用mbSerial (PB10/PB11) 和 PB1方向控制 */
#define SENSOR_RS485_EN mbSendEnPin // RS485方向控制引脚，使用原有定义

/* 串口屏UART7引脚已在IO_Setting.h中定义 */
// #define HMI_USART_RX PC10 // UART7 RX
// #define HMI_USART_TX PA15 // UART7 TX

/* 传感器Modbus参数 */
#define SENSOR_SLAVE_ID     0x01
#define SENSOR_FUNC_CODE    0x03
#define SENSOR_BAUDRATE     19200  /* 与串口屏波特率一致 */

/* 寄存器地址 */
#define REG_TEMP_HUMID_START 0x0000
#define REG_TEMP_HUMID_COUNT 0x0002

/* 响应帧长度 */
#define RESP_TEMP_HUMID_LEN  9  // 01 03 04 HH HL TH TL CRCL CRCH

/* ==================== 串口屏通信协议 ==================== */
/*
 * 自定义帧协议（帧头==帧尾），与淘晶驰VT内部协议混合传输
 *
 * STM32 → 串口屏（传感器数据发送）:
 *   0x03: 温度  0x04: 湿度  0x05: CO2  0x06: NH3
 *   格式: [flag][字符串数据][flag]
 *
 * 串口屏 → STM32（设备控制）:
 *   0x10~0x80: 8路设备开关  格式: [head][value][head] (3字节)
 *   0x0A~0x0D: 阈值设置    格式: [head][value_text][head] (3字节)
 *   0x0E/0x0F: 参数控制    格式: [head][val1][val2][head] (4字节)
 *   0x01: 数值参数         格式: [head][co1H][co1L][co2H][co2L][head] (6字节)
 *   0x02: 参数组           格式: [head][val1][val2][head] (4字节)
 *
 * VT协议过滤: 以0xEE开头的VT内部帧自动丢弃
 */

/* STM32 → 串口屏 传感器数据标志 */
#define HMI_FLAG_TEMP        0x03
#define HMI_FLAG_HUMID       0x04
#define HMI_FLAG_CO2         0x05
#define HMI_FLAG_NH3         0x06

/* 串口屏 → STM32 设备控制命令头 */
#define HMI_CMD_THRESHOLD_A  0x0A  // 阈值A
#define HMI_CMD_THRESHOLD_B  0x0B  // 阈值B
#define HMI_CMD_THRESHOLD_C  0x0C  // 阈值C
#define HMI_CMD_THRESHOLD_D  0x0D  // 阈值D
#define HMI_CMD_PARAM_E      0x0E  // 参数控制E
#define HMI_CMD_PARAM_F      0x0F  // 参数控制F
#define HMI_CMD_NUMERIC      0x01  // 数值参数(6字节)
#define HMI_CMD_PARAM_GROUP  0x02  // 参数组(4字节)
#define HMI_CMD_DEV_BASE     0x10  // 设备控制起始码 0x10~0x80
#define HMI_CMD_DEV_END      0x80  // 设备控制结束码
#define HMI_VT_HEADER        0xEE  // VT内部协议帧头(需过滤)

/* 传感器Modbus寄存器地址定义 (使用25-29，避开LoRa的20-23和AI0-AI3的15-18) */
#define REG_SENSOR_TEMP       25 // 温度值 (放大10倍，如256表示25.6度)
#define REG_SENSOR_HUMID      26 // 湿度值 (放大10倍)
#define REG_SENSOR_CO2        27 // CO2浓度 (ppm，原始值)
#define REG_SENSOR_NH3        28 // NH3浓度 (放大100倍，如123表示1.23ppm)
#define REG_SENSOR_STATUS     29 // 传感器状态位: bit0=温湿度有效, bit1=CO2有效, bit2=NH3有效

/* ==================== 设备控制引脚映射 ==================== */
/*
 * 8路继电器 (PE7~PE14)，设备码 → 输出引脚 → Modbus Hreg(12)位映射:
 *   Y1~Y4=加湿器, Y5~Y8=风机
 *   0x10: 加湿器1 → Y1(PE7)  → bit0
 *   0x20: 加湿器2 → Y2(PE8)  → bit1
 *   0x30: 加湿器3 → Y3(PE9)  → bit2
 *   0x40: 加湿器4 → Y4(PE10) → bit3
 *   0x50: 风机1   → Y5(PE11) → bit4
 *   0x60: 风机2   → Y6(PE12) → bit5
 *   0x70: 风机3   → Y7(PE13) → bit6
 *   0x80: 风机4   → Y8(PE14) → bit7
 */
// 根据设备命令头获取对应的Modbus输出寄存器bit位
inline uint8_t getDeviceBitFromHead(uint8_t head) {
    if (head >= 0x10 && head <= 0x80) {
        // 0x10→bit0, 0x20→bit1, ..., 0x80→bit7
        return ((head >> 4) & 0x0F) - 1;
    }
    return 0;
}

/* 风机对应输出引脚 (Y5~Y8: PE11~PE14) */
#define FAN1_PIN Output_Y5 // 风机1 -> Y5(PE11)
#define FAN2_PIN Output_Y6 // 风机2 -> Y6(PE12)
#define FAN3_PIN Output_Y7 // 风机3 -> Y7(PE13)
#define FAN4_PIN Output_Y8 // 风机4 -> Y8(PE14)

/* 加湿器对应输出引脚 (Y1~Y4: PE7~PE10) */
#define HUMI1_PIN Output_Y1 // 加湿器1 -> Y1(PE7)
#define HUMI2_PIN Output_Y2 // 加湿器2 -> Y2(PE8)
#define HUMI3_PIN Output_Y3 // 加湿器3 -> Y3(PE9)
#define HUMI4_PIN Output_Y4 // 加湿器4 -> Y4(PE10)

/* 传感器轮询周期 - 5秒 */
#define SENSOR_POLL_INTERVAL_MS 5000

/* 传感器互斥锁 - 三个传感器任务和ModbusRTU任务共用RS485总线 */
extern SemaphoreHandle_t xSensorMutex;

/* 设备状态结构体和互斥锁 - 定义在myDeviceState.h中，避免循环依赖 */
#include "myDeviceState.h"

/* 传感器串口使用板子原有的mbSerial (PB10/PB11)，不再新建HardwareSerial实例 */

/* 串口屏串口实例 */
extern SoftwareSerial HMISerial;

/* 函数声明 */
uint16_t ModbusCRC16(uint8_t *data, uint8_t length);
uint8_t BuildModbusFrame(uint8_t *frame, uint8_t slaveId, uint8_t funcCode, uint16_t regStart, uint16_t regCount);
void SendSensorFrame(uint8_t *frame, uint8_t len);
uint8_t ReceiveSensorResponse(uint8_t *buffer, uint8_t expectedLen, uint32_t timeoutMs);
bool VerifyResponseCRC(uint8_t *buffer, uint8_t len);
void SendToHMI(uint8_t flag, float value, uint8_t decimalPlaces);
void SensorSerial_Init();
void ProcessDeviceControl(uint8_t head, uint8_t value);
void HMIReceiveTask(void *pvParameters);

/************************************************************************************
 * 传感器状态机定义
 ************************************************************************************/
enum SensorState
{
    STATE_READ_TEMP_HUMID = 0,  // 读取温湿度
    STATE_SEND_HMI,             // 发送数据到串口屏
    STATE_WAIT_INTERVAL         // 等待轮询间隔
};

void SensorStateMachineTask(void *pvParameters);

#endif
