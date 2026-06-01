/**
 * @file myADS1115.h
 * @brief 用于ADS1115的库
 * @version 1.0
 * @date 2024-03-25
 * @author  Neo
 *
 * 状态：已在FreeRTOS中使用单独任务测试完成
 *
 * 该库是对robtillaart/ADS1X15@^0.4.2的二次封装
 * 该库是基于ADS1115的I2C模数转换器库，可以通过I2C读取4个模拟信号
 * 该库的使用需要先初始化I2C，然后初始化ADS1115，设置增益，然后就可以通过读写函数读取模拟信号
 * 使用说明：
 * 1. 首先需要初始化I2C，然后初始化ADS1115
 * 2. 设置增益，然后通过读写函数读取模拟信号
 *
 */

#ifndef _my_ADS1115_H_
#define _my_ADS1115_H_

#include <Arduino.h>
#include <ADS1X15.h>
#include "myShowMsg.h"
// 定义ADS1115的I2C地址,默认地址为0x48
ADS1115 ADS(0x48);

/**
 * @brief 初始化ADS1115
 * @return 初始化成功返回true，失败返回false
*/
bool InitializeADS1115()
{
    // Wire.setSDA(PB9);   // 设置I2C的SDA和SCL引脚
    // Wire.setSCL(PB8);   // 设置I2C的SDA和SCL引脚
    // Wire.begin();       // 初始化Wire库
    ADS.setGain(1);     // 0: 6.144V, 1: 4.096V, 2: 2.048V, 3: 1.024V, 4: 0.512V, 5: 0.256V
    return ADS.begin(); // 初始化ADS1115
}
/**
 * @brief 读取ADS1115的指定通道的模拟信号
 * @param channel 通道号，0-3
 * @return 通道的模拟信号
*/
float ReadADS1115(int channel)
{
    int16_t val = ADS.readADC(channel);
    float f = ADS.toVoltage(1); //  voltage factor
    ShowMsg("A" + String(channel) + ":" + String(val) + "\t" + String(val * f, 3), true);
    return val * f;
}
/**
 * @brief 读取ADS1115的全部4个通道的模拟信号
 * @param val_0 通道0的模拟信号
 * @param val_1 通道1的模拟信号
 * @param val_2 通道2的模拟信号
 * @param val_3 通道3的模拟信号
*/
void ReadADS1115All(int16_t &val_0, int16_t &val_1, int16_t &val_2, int16_t &val_3)
{
    val_0 = ADS.readADC(0);
    val_1 = ADS.readADC(1);
    val_2 = ADS.readADC(2);
    val_3 = ADS.readADC(3);
}

/**
 * @brief 读取ADS1115的全部4个通道的模拟信号并打印
*/
void ReadADS1115All()
{
    int16_t val_0 = ADS.readADC(0);
    int16_t val_1 = ADS.readADC(1);
    int16_t val_2 = ADS.readADC(2);
    int16_t val_3 = ADS.readADC(3);

    float f = ADS.toVoltage(1); //  voltage factor
    ShowMsg("Read ADS1115:");
    ShowMsg("\nA0:\t" + String(val_0) + "\t" + String(val_0 * f, 3) + "V");
    ShowMsg("\nA1:\t" + String(val_1) + "\t" + String(val_1 * f, 3) + "V");
    ShowMsg("\nA2:\t" + String(val_2) + "\t" + String(val_2 * f, 3) + "V");
    ShowMsg("\nA3:\t" + String(val_3) + "\t" + String(val_3 * f, 3) + "V", true);
}

#endif