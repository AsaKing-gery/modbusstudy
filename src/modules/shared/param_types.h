/**
 * @file    param_types.h
 * @brief   系统参数结构体定义（共享层，app 和 modules 均可安全引用）
 * @note    仅包含数据结构定义，不包含持久化逻辑
 */

#ifndef SHARED_PARAM_TYPES_H_
#define SHARED_PARAM_TYPES_H_

#include <Arduino.h>
#include <IPAddress.h>

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
    uint16_t threshold[4];        /**< 阈值 A/B/C/D (页面9) */
    uint16_t temp_hi_x100;        /**< 温度上限 ×100 (页面3控件26) */
    uint16_t temp_lo_x100;        /**< 温度下限 ×100 */
    uint16_t humi_hi_x100;        /**< 湿度上限 ×100 (页面3控件27) */
    uint16_t humi_lo_x100;        /**< 湿度下限 ×100 */
    uint16_t co2_hi;              /**< CO2 上限 ppm (页面3控件28) */
    uint16_t co2_lo;              /**< CO2 下限 ppm */
    uint16_t nh3_hi_x100;         /**< NH3 上限 ×100 (页面3控件29) */
    uint16_t nh3_lo_x100;         /**< NH3 下限 ×100 */

    /** 默认构造函数 */
    SystemParam()
        : init_flag(66)
        , slave_id(1)
        , baudrate(115200)
        , mac{0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56}
        , ip{192, 168, 1, 168}
        , input_filter_ms(5)
        , temp_hi_x100(3500), temp_lo_x100(2400)
        , humi_hi_x100(7000), humi_lo_x100(5000)
        , co2_hi(3000), co2_lo(0)
        , nh3_hi_x100(2000), nh3_lo_x100(0)
    {
        threshold[0] = threshold[1] = threshold[2] = threshold[3] = 0;
    }
};

/* ========================== 全局参数实例 ========================== */
extern SystemParam g_param;

#endif /* SHARED_PARAM_TYPES_H_ */
