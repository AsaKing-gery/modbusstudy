/**
 * @file    param.cpp
 * @brief   参数管理实现 - 完整 EEPROM 持久化
 */

#include "param.h"
#include <EEPROM.h>
#include <IWatchdog.h>
#include "app/app_debug.h"
#include <stm32f4xx_hal_flash.h>

/* 全局参数单例 */
SystemParam g_param;

/* ========================== MCU ID ========================== */
uint32_t param_get_mcu_id(uint8_t offset_index)
{
    if (offset_index > 2) offset_index = 2;
    return *(uint32_t *)(0x1FFF7A10 + (offset_index * 4));
}

void param_generate_mac(void)
{
    uint32_t id1 = param_get_mcu_id(0);
    uint32_t id2 = param_get_mcu_id(1);
    g_param.mac[0] = id1 & 0xFF;
    g_param.mac[1] = (id1 >> 8) & 0xFF;
    g_param.mac[2] = (id1 >> 16) & 0xFF;
    g_param.mac[3] = (id1 >> 24) & 0xFF;
    g_param.mac[4] = id2 & 0xFF;
    g_param.mac[5] = (id2 >> 8) & 0xFF;
}

/* ========================== 默认值 ========================== */
void param_init_defaults(void)
{
    g_param = SystemParam();
    param_generate_mac();
}

/* ========================== EEPROM 读写 ========================== */
void param_load(void)
{
    TRACE("L");

    /* 清除上次 Flash 写入中断可能残留的错误标志, 防止 EEPROM.begin() 死循环 */
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR
                         | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    EEPROM.begin();

    if (EEPROM.read(EE_OFFSET_FLAG) == 66) {
        /* 有效数据，加载 */
        g_param.init_flag        = 66;
        g_param.slave_id         = EEPROM.read(EE_OFFSET_SLAVEID);
        EEPROM.get(EE_OFFSET_BAUDRATE, g_param.baudrate);
        for (int i = 0; i < 6; i++) g_param.mac[i] = EEPROM.read(EE_OFFSET_MAC + i);
        for (int i = 0; i < 4; i++) g_param.ip[i]  = EEPROM.read(EE_OFFSET_IP + i);
        EEPROM.get(EE_OFFSET_FILTER,     g_param.input_filter_ms);
        for (int i = 0; i < 4; i++)
            EEPROM.get(EE_OFFSET_THRESHOLD + i * 2, g_param.threshold[i]);
        /* 页面3传感器阈值 */
        EEPROM.get(EE_OFFSET_TEMP_HI, g_param.temp_hi_x100);
        EEPROM.get(EE_OFFSET_TEMP_LO, g_param.temp_lo_x100);
        EEPROM.get(EE_OFFSET_HUMI_HI, g_param.humi_hi_x100);
        EEPROM.get(EE_OFFSET_HUMI_LO, g_param.humi_lo_x100);
        EEPROM.get(EE_OFFSET_CO2_HI,  g_param.co2_hi);
        EEPROM.get(EE_OFFSET_CO2_LO,  g_param.co2_lo);
        EEPROM.get(EE_OFFSET_NH3_HI,  g_param.nh3_hi_x100);
        EEPROM.get(EE_OFFSET_NH3_LO,  g_param.nh3_lo_x100);

        /* 过滤未编程EEPROM的脏数据(0xFFFF → 默认值) */
        if (g_param.temp_hi_x100 == 0xFFFF) g_param.temp_hi_x100 = 3500;
        if (g_param.temp_lo_x100 == 0xFFFF) g_param.temp_lo_x100 = 2400;
        if (g_param.humi_hi_x100 == 0xFFFF) g_param.humi_hi_x100 = 7000;
        if (g_param.humi_lo_x100 == 0xFFFF) g_param.humi_lo_x100 = 5000;
        if (g_param.co2_hi      == 0xFFFF) g_param.co2_hi      = 3000;
        if (g_param.co2_lo      == 0xFFFF) g_param.co2_lo      = 0;
        if (g_param.nh3_hi_x100 == 0xFFFF) g_param.nh3_hi_x100 = 2000;
        if (g_param.nh3_lo_x100 == 0xFFFF) g_param.nh3_lo_x100 = 0;
    } else {
        /* 首次启动或数据损坏，初始化默认值并写入 */
        param_init_defaults();
        param_save();
    }

    TRACE("P");
}

void param_save(void)
{
    /* 喂狗: EEPROM 擦写 Flash 可能超过 WDT 超时 */
    IWatchdog.reload();
    EEPROM.update(EE_OFFSET_FLAG,    g_param.init_flag);
    EEPROM.update(EE_OFFSET_SLAVEID, g_param.slave_id);
    EEPROM.put(EE_OFFSET_BAUDRATE,   g_param.baudrate);
    for (int i = 0; i < 6; i++) EEPROM.update(EE_OFFSET_MAC + i, g_param.mac[i]);
    for (int i = 0; i < 4; i++) EEPROM.update(EE_OFFSET_IP + i,   g_param.ip[i]);
    EEPROM.put(EE_OFFSET_FILTER,     g_param.input_filter_ms);
    for (int i = 0; i < 4; i++)
        EEPROM.put(EE_OFFSET_THRESHOLD + i * 2, g_param.threshold[i]);
    /* 页面3传感器阈值 */
    EEPROM.put(EE_OFFSET_TEMP_HI, g_param.temp_hi_x100);
    EEPROM.put(EE_OFFSET_TEMP_LO, g_param.temp_lo_x100);
    EEPROM.put(EE_OFFSET_HUMI_HI, g_param.humi_hi_x100);
    EEPROM.put(EE_OFFSET_HUMI_LO, g_param.humi_lo_x100);
    EEPROM.put(EE_OFFSET_CO2_HI,  g_param.co2_hi);
    EEPROM.put(EE_OFFSET_CO2_LO,  g_param.co2_lo);
    EEPROM.put(EE_OFFSET_NH3_HI,  g_param.nh3_hi_x100);
    EEPROM.put(EE_OFFSET_NH3_LO,  g_param.nh3_lo_x100);

    DBG("PARAM", "saved to EEPROM");
}

void param_factory_reset(void)
{
    param_init_defaults();
    param_save();
    DBG("PARAM", "factory reset");
}
