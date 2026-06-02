#ifndef _my_DEVICE_STATE_H_
#define _my_DEVICE_STATE_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>

/* 设备状态变量 - 4个风机 + 4个加湿器 + 本地传感器数据 + LoRa机器人传感器数据
 * 该头文件被mySensorTask.h和myMQTT_TLS.h共享，避免循环依赖和重复定义
 */
struct DeviceState {
    bool fan[4];              // 风机1-4状态
    bool humidifier[4];       // 加湿器1-4状态
    float temperature;        // 本地温度
    float humidity;           // 本地湿度
    float co2;                // 本地CO2浓度
    float nh3;                // 本地NH3浓度
    float loraTemperature;    // LoRa机器人采集温度
    float loraHumidity;       // LoRa机器人采集湿度
    float loraCo2;            // LoRa机器人采集CO2浓度
    float loraNh3;            // LoRa机器人采集NH3浓度
};

/* 传感器数据互斥锁 - 保护传感器数据读取 */
extern SemaphoreHandle_t xSensorDataMutex;

/* 设备状态全局变量 */
extern DeviceState deviceState;

#endif
