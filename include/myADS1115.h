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

// 声明ADS1115的I2C实例,定义在globals.cpp中
extern ADS1115 ADS;

/* 函数声明 */
bool InitializeADS1115();
float ReadADS1115(int channel);
void ReadADS1115All(int16_t &val_0, int16_t &val_1, int16_t &val_2, int16_t &val_3);
void ReadADS1115All();

#endif
