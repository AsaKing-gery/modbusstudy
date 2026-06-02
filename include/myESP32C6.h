#ifndef _my_ESP32C6_H_
#define _my_ESP32C6_H_

#include <Arduino.h>
#include <SPI.h>
#include "myShowMsg.h"

/* ESP32C6使用SPI3实例，与Ra-01S的SPI2完全隔离，避免总线冲突
 * 定义在globals.cpp中
 */
extern SPIClass ESP32C6_SPI3;

/* 函数声明 */
void ESP32C6_SPI_Init();

#endif
