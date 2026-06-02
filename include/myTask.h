#ifndef _my_Task_H_
#define _my_Task_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <IWatchdog.h>
#include "IO_Setting.h"
#include "myModbus.h"
#include "myADS1115.h"
#include "mySensorTask.h"
#include "myLoRaTask.h"
#include "myMQTT_TLS.h"
// #include "myExternaIO.h"

// 设定字中的位状态
#define SET_BIT_BY_BOOL(uint16_t, bitIndex, value) \
    ((value) ? ((uint16_t) |= (1 << (bitIndex))) : ((uint16_t) &= ~(1 << (bitIndex))))

// 定义是否开启任务堆栈剩余空间测试功能
// #define TaskStackTestEnable 1

// Watchdog超时时间，单位为毫秒
#define WATCHDOG_TIMEOUT_MS 400

/************************************************************************************
任务列表：
************************************************************************************/
/**
 * @brief Watchdog定时任务
 */
void WatchdogTask(void *pvParameters);

#ifdef TaskStackTestEnable
// 定义任务句柄,用来测试任务堆栈剩余空间,将&taskTest放在任务中获取句柄
extern TaskHandle_t taskTest;

/**
 * @brief 任务测试函数,用来测试任务堆栈剩余空间
 */
void TaskStackTest(void *pvParameters);
#endif

// IIC任务,本打算用来读取ADS1115的数值，但总是无法正确读取，这里就先取消
//  /**
//   * @brief IIC任务
//   */
void IICTask(void *pvParameters);

/**
 * 将参数加载到MB寄存器中
 */
void Load_ParameterTORegister(void);

/**
 * @brief 将MB寄存器参数保存到参数变量中,同时保存到EEPROM
 */
void Save_ParameterFromRegister();

/// @brief 主任务
/// @param pvParameters
void MainTask(void *pvParameters);

/**
 * @brief 任务初始化函数
 * @note 该函数在系统启动时调用，用来创建任务
 * @return void
 */
void CreateTaskMethods(void *pvParameters);
#endif