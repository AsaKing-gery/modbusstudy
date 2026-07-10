/**
 * @file    modbus_rtu.h
 * @brief   Modbus RTU 从站接口
 */

#ifndef MODBUS_RTU_H_
#define MODBUS_RTU_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

#define FIRMWARE_VERSION      40501
#define FIRMWARE_VERSION_STR  "4.05.01"

void modbus_rtu_init(void);
void modbus_rtu_task(void *pvParameters);

#endif /* MODBUS_RTU_H_ */
