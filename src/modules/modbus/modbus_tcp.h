/**
 * @file    modbus_tcp.h
 * @brief   Modbus TCP 服务器（通过 ESP32 WiFi 桥接，占位）
 */

#ifndef MODBUS_TCP_H_
#define MODBUS_TCP_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void modbus_tcp_init(void);
void modbus_tcp_task(void *pvParameters);

#endif /* MODBUS_TCP_H_ */
