/**
 * @file    param.h
 * @brief   参数管理 - EEPROM 存储与加载
 * @note    所有可配置参数均持久化到 Flash 模拟 EEPROM
 */

#ifndef APP_PARAM_H_
#define APP_PARAM_H_

#include <Arduino.h>
#include <IPAddress.h>
#include "bsp/bsp_config.h"
#include "../modules/shared/param_types.h"

/* ========================== EEPROM 布局 ========================== */
/*
 * Offset  0: init_flag         (1B)  必须是 66 才有效
 * Offset  1: slave_id          (1B)
 * Offset  2: baudrate          (4B)
 * Offset  6: mac[6]            (6B)
 * Offset 12: ip[4]             (4B)
 * Offset 16: input_filter_ms   (2B)
 * Offset 18: threshold[4]      (8B)
 * Offset 26: temp_hi_x100      (2B)
 * Offset 28: temp_lo_x100      (2B)
 * Offset 30: humi_hi_x100      (2B)
 * Offset 32: humi_lo_x100      (2B)
 * Offset 34: co2_hi            (2B)
 * Offset 36: co2_lo            (2B)
 * Offset 38: nh3_hi_x100       (2B)
 * Offset 40: nh3_lo_x100       (2B)
 * Total: 42 字节
 */
#define EE_OFFSET_FLAG          0
#define EE_OFFSET_SLAVEID       1
#define EE_OFFSET_BAUDRATE      2
#define EE_OFFSET_MAC           6
#define EE_OFFSET_IP            12
#define EE_OFFSET_FILTER        16
#define EE_OFFSET_THRESHOLD     18
#define EE_OFFSET_TEMP_HI       26
#define EE_OFFSET_TEMP_LO       28
#define EE_OFFSET_HUMI_HI       30
#define EE_OFFSET_HUMI_LO       32
#define EE_OFFSET_CO2_HI        34
#define EE_OFFSET_CO2_LO        36
#define EE_OFFSET_NH3_HI        38
#define EE_OFFSET_NH3_LO        40
#define EE_TOTAL_SIZE           42

/* ========================== 函数声明 ========================== */

/**
 * @brief 从 MCU UID 生成 MAC 地址
 */
void param_generate_mac(void);

/**
 * @brief 获取 MCU 96位 UID 中的 32 位字
 */
uint32_t param_get_mcu_id(uint8_t offset_index);

/**
 * @brief 初始化参数（用 MCU UID 生成 MAC + 默认值）
 */
void param_init_defaults(void);

/**
 * @brief 从 EEPROM 加载参数，无效则初始化并写入
 */
void param_load(void);

/**
 * @brief 保存参数到 EEPROM
 */
void param_save(void);

/**
 * @brief 将参数加载到 Modbus 保持寄存器
 */
void param_to_registers(void);

/**
 * @brief 从 Modbus 保持寄存器保存参数
 */
void param_from_registers(void);

/**
 * @brief 恢复出厂默认值
 */
void param_factory_reset(void);

#endif /* APP_PARAM_H_ */
