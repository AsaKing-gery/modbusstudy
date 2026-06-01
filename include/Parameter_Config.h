#ifndef _PARAMETER_CONFIG_H_
#define _PARAMETER_CONFIG_H_
#include <Arduino.h>
#include <EEPROM.h>
#include <IPAddress.h>
#include "myShowMsg.h"

#define MaxModbusRegNum 20 // 最大寄存器数量
#define Version 40501      // 固件版本号,一位年尾号+2位月份+2位日

/**
 * @brief 获取MCU唯一标识符，MCUID由一个96位的唯一标识符组成，通过读取MCU的唯一标识符，可以唯一标识该MCU
 * @param offsetIndex 偏移索引，用于获取不同MCU的唯一标识符，默认0,范围0-3
 * @return MCU唯一标识符
 */
uint32_t GetMCUId(uint8_t offsetIndex = 0)
{
    offsetIndex > 3 ? offsetIndex = 3 : 0;
    return *(uint32_t *)(0x1FFFF7E8 + (offsetIndex * 4));
}

/**
 * @brief 设备操作选项
 *
 * 设备操作选项，用于控制设备的各种操作，包括保存参数配置、加载参数配置、重启设备、恢复出厂设置
 */
enum ParameterOption
{
    Save = 10,         // 将Modbus寄存器的值保存到EEPROM中
    Reload = 20,       // 从EEPROM中加载Modbus寄存器的值
    Reboot = 30,       // 重启设备
    Factory_Reset = 66 // 恢复出厂设置并重启设备
};

/**
 * @brief 参数配置结构体
 *
 * 参数配置结构体，用于保存设备的参数配置，包括初始标志、站号、波特率、MAC地址、IP地址、输入滤波时间
 */
struct Parameter_Config
{
    uint8_t InitFlag;  // 初始标志，用于判断是否是第一次启动，如果是第一次启动，则需要恢复出厂设置，否则加载参数配置
    uint8_t SlaveId;   // 站号,默认站号1,如果需要波特率可配置，可将该参数设定成变量，在串口初始化之前设置
    uint32_t Baudrate; // 波特率,默认波特率115200,如果需要波特率可配置，可将该参数设定成变量，在串口初始化之前设置

    byte mac[6];  // MAC地址，默认MAC地址由MCU唯一标识符生成后4个字节，如果需要MAC地址可配置，可将该参数设定成变量，在网络初始化之前设置
    IPAddress ip; // IP地址,默认IP地址192.168.1.168,如果需要IP地址可配置，可将该参数设定成变量，在网络初始化之前设置

    uint16_t Input_Filter_Time; // 输入滤波时间，单位ms，范围1-100
    Parameter_Config() : InitFlag(66), SlaveId(1), Baudrate(115200), mac{0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56}, ip{192, 168, 1, 168}, Input_Filter_Time(5) {}
};

// 全局变量，保存参数配置
Parameter_Config myPar = Parameter_Config();

/**
 * @brief 保存参数配置到EEPROM
 *
 * 保存参数配置到EEPROM
 */
void Save_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Saving parameter", true);
    EEPROM.put(0, myPar);
    ShowMsg("Parameter Saved:" + String(millis() - recordTime), true);
}

/**
 * @brief 参数初始化
 */
void Parameter_Init()
{
    ShowMsg("Initializing parameter", true);
    myPar = Parameter_Config();
    uint32_t id1 = GetMCUId();  // 获取MCU低32位ID
    uint32_t id2 = GetMCUId(1); // 获取MCU高32位ID
    myPar.mac[0] = id1 & 0xFF;
    myPar.mac[1] = (id1 >> 8) & 0xFF;
    myPar.mac[2] = (id1 >> 16) & 0xFF;
    myPar.mac[3] = (id1 >> 24) & 0xFF;
    myPar.mac[4] = id2 & 0xFF;
    myPar.mac[5] = (id2 >> 8) & 0xFF;
    Save_Parameter();
}

/**
 * @brief 从EEPROM中加载参数配置
 *
 * 从EEPROM中加载参数配置，如果是第一次下载程序，则恢复出厂设置
 */
void Load_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Loading parameter", true);
    EEPROM.get(0, myPar);     // 从EEPROM中读取参数配置
    if (myPar.InitFlag != 66) // 如果是第一次下载程序，myPar.InitFlag肯定不是66，则直接调用默认设置并保存到EEPROM，那么下次启动时，myPar.InitFlag肯定是66，则加载参数配置
    {
        Parameter_Init();
    }
    ShowMsg("Parameter loaded：" + String(millis() - recordTime), true);
}

#endif