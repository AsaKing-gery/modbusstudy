/**
 * @file    modbus_core.h
 * @brief   Modbus 核心 - 寄存器表管理（RTU 和 TCP 共享同一份寄存器表）
 */

#ifndef MODBUS_CORE_H_
#define MODBUS_CORE_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "bsp/bsp_config.h"

/* ========================== 寄存器表访问 ========================== */
void     modbus_regs_init(void);
uint16_t modbus_reg_get(uint8_t addr);
void     modbus_reg_set(uint8_t addr, uint16_t value);

/* ========================== 参数同步 ========================== */
/** 将 g_param 加载到 Modbus 寄存器表 */
void modbus_load_from_param(void);

/** 从 Modbus 寄存器表保存到 g_param 并写入 EEPROM */
void modbus_save_to_param(void);

/** 处理命令寄存器 (REG_COMMAND): 保存/重载/重启/恢复出厂 */
void modbus_handle_command(void);

/* ========================== 互斥锁 ========================== */
extern SemaphoreHandle_t g_rs485_mutex;

#endif /* MODBUS_CORE_H_ */
