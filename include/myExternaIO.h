
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

// 定义PCF8575的I2C地址,同时注意使用的IIC引脚
PCF8575 pcf8575(0x20);

/**
 * @brief 初始化外部IO
 *
 * @param mode 设置引脚的模式，默认为0x00FF，高8位为输出，低8位为输入,对应bit为0为输出低电平，1为高电平或者输入
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool ExternalIOInitialize(uint16_t mode = 0xFFFF)
{

    Wire.setSDA(PB9);
    Wire.setSCL(PB8);
    Wire.begin();               // 初始化Wire
    return pcf8575.begin(mode); // 初始化PCF8575,设置高8位为输出，低8位为输入,对应bit为0为输出低电平，1为高电平或者输入
}
/**
 * @brief 设置外部IO的模式,也可以在PDF8575.begin中就设置
 *
 * @param mode 16个bit，bit0-bit7=>P0-P7,bit8-bit15=>P10-P17，对应bit为0为输出低电平，1为高电平或者输入
 *
 */
void ExternalSetPinMode(uint16_t mode)
{
    pcf8575.write16(mode);
}

/**
 * @brief 设置外部某个bit的的状态
 */
void ExternalDigitalWrite(uint8_t pin, uint8_t value)
{
    pcf8575.write(pin, value);
}

/**
 * @brief 设置外部所有bit的状态
 *
 * @param value 16个bit，bit0-bit7=>P0-P7,bit8-bit15=>P10-P17，对应bit为0为输出低电平，1为高电平或者输入
 */
void ExternalDigitalWriteAll(uint16_t value)
{
    pcf8575.write16(value);
}

/**
 * @brief 读取外部某个bit的状态
 *
 * @return true 为高电平
 * @return false 为低电平
 */
bool ExternalDigitalRead(uint8_t pin)
{
    return pcf8575.read(pin) > 0 ? true : false;
}

/**
 * @brief 读取外部所有bit的状态
 *
 * @return uint16_t 16个bit，bit0-bit7=>P0-P7,bit8-bit15=>P10-P17，对应bit为0为输出低电平，1为高电平
 */
uint16_t ExternalDigitalReadAll()
{
    return pcf8575.read16();
}

/**
 * @brief 翻转外部某个bit的状态
 *
 * @param pin 需要反转的bit
 */
void ExternalDigitalToggle(uint8_t pin)
{
    pcf8575.toggle(pin);
}

/**
 * @brief IIC扫描,扫描IIC总线上的设备，注意这里引脚的设置
 */
void IICScan()
{
    Wire.setSDA(PB9);
    Wire.setSCL(PB8);
    Wire.begin(); // 初始化Wire
    for (uint8_t i = 0; i < 127; i++)
    {
        Wire.beginTransmission(i);
        //ShowMsg("IIC scan：" + String(i), true);
        if (Wire.endTransmission() == 0)
        {
            ShowMsg("I2C device found at address 0x");
            if (i < 16)
                ShowMsg("0");
            ShowMsg(String(i, HEX));
            ShowMsg("  !", true);
        }
    }
}
#endif