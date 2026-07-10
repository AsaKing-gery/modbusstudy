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

/* ========================== 参数命令 ========================== */
enum ParamCommand
{
    CMD_SAVE         = 10,    /**< 保存参数到 EEPROM */
    CMD_RELOAD       = 20,    /**< 从 EEPROM 重载参数 */
    CMD_REBOOT       = 30,    /**< 软件复位 */
    CMD_FACTORY_RESET = 66    /**< 恢复出厂设置并重启 */
};

/* ========================== 参数结构体 ========================== */
struct SystemParam
{
    uint8_t  init_flag;           /**< 有效标志，固定 66 */
    uint8_t  slave_id;            /**< Modbus 从站 ID */
    uint32_t baudrate;            /**< RS485 波特率 */
    uint8_t  mac[6];              /**< MAC 地址 */
    uint8_t  ip[4];               /**< 静态 IP */
    uint16_t input_filter_ms;     /**< 输入滤波时间 ms */
    uint16_t threshold[4];        /**< 阈值 A/B/C/D */

    /** 默认构造函数 */
    SystemParam()
        : init_flag(66)
        , slave_id(1)
        , baudrate(115200)
        , mac{0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56}
        , ip{192, 168, 1, 168}
        , input_filter_ms(5)
    {
        threshold[0] = threshold[1] = threshold[2] = threshold[3] = 0;
    }
};

/* ========================== 全局参数实例 ========================== */
extern SystemParam g_param;

/* ========================== EEPROM 布局 ========================== */
/*
 * Offset  0: init_flag         (1B)  必须是 66 才有效
 * Offset  1: slave_id          (1B)
 * Offset  2: baudrate          (4B)
 * Offset  6: mac[6]            (6B)
 * Offset 12: ip[4]             (4B)
 * Offset 16: input_filter_ms   (2B)
 * Offset 18: threshold[4]      (8B)
 * Total: 26 字节
 */
#define EE_OFFSET_FLAG          0
#define EE_OFFSET_SLAVEID       1
#define EE_OFFSET_BAUDRATE      2
#define EE_OFFSET_MAC           6
#define EE_OFFSET_IP            12
#define EE_OFFSET_FILTER        16
#define EE_OFFSET_THRESHOLD     18
#define EE_TOTAL_SIZE           26

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
