
/**
 * @file myExternalIO.h
 * @brief 用于STM32的外部IO扩展库
 * @version 1.0
 * @date 2024-03-25
 * @author  Neo
 *
 * 状态：已在FreeRTOS中使用单独任务测试完成
 *
 * 该库是对robtillaart/PCF8575@^0.2.2的二次封装
 * 该库是基于PCF8575的I2C外部IO扩展库，可以通过I2C控制16个IO口，每个IO口可以设置为输入或输出 *
 * 该库的使用需要先初始化I2C，然后初始化PCF8575，设置IO口的输入输出模式，然后就可以通过读写函数控制IO口 *
 * 该库还提供了IIC扫描函数，可以扫描IIC总线上的设备
 * 使用说明：
 * 1. 首先需要初始化I2C，然后初始化PCF8575
 * 2. 设置IO口的输入输出模式，也可以在初始化PCF8575时设置直接设置，主要是通过16个bit来设置，bit0-bit7=>P0-P7,bit8-bit15=>P10-P17，对应bit为0为输出低电平，1为高电平或者输入
 * 3. 通过读写函数控制IO口
 *
 */

#ifndef _my_EXTERNAL_IO_H_
#define _my_EXTERNAL_IO_H_

#include <Arduino.h>
#include <PCF8575.h>
#include <Wire.h>
#include "myShowMsg.h"

// 声明PCF8575的I2C实例,定义在globals.cpp中
extern PCF8575 pcf8575;

/* 函数声明 */
bool ExternalIOInitialize(uint16_t mode = 0xFFFF);
void ExternalSetPinMode(uint16_t mode);
void ExternalDigitalWrite(uint8_t pin, uint8_t value);
void ExternalDigitalWriteAll(uint16_t value);
bool ExternalDigitalRead(uint8_t pin);
uint16_t ExternalDigitalReadAll();
void ExternalDigitalToggle(uint8_t pin);
void IICScan();

#endif
