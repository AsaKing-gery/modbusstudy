#ifndef _my_K210_H_
#define _my_K210_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "IO_Setting.h"
#include "myShowMsg.h"

// K210摄像头模块串口定义 - 定义在globals.cpp中
extern HardwareSerial k210Serial;

/* 函数声明 */
void K210_Initialize(uint32_t baudrate = 115200);
void K210_Task(void *pvParameters);

#endif
