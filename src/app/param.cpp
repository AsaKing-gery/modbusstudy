/**
 * @file    param.cpp
 * @brief   参数管理实现 - 完整 EEPROM 持久化
 */

#include "param.h"
#include <EEPROM.h>
#include "app/app_debug.h"

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

    EEPROM.begin();

    if (EEPROM.read(EE_OFFSET_FLAG) == 66) {
        /* 有效数据，加载 */
        g_param.init_flag = 66;
        g_param.slave_id  = EEPROM.read(EE_OFFSET_SLAVEID);
        EEPROM.get(EE_OFFSET_BAUDRATE, g_param.baudrate);
        for (int i = 0; i < 6; i++) g_param.mac[i] = EEPROM.read(EE_OFFSET_MAC + i);
        for (int i = 0; i < 4; i++) g_param.ip[i]  = EEPROM.read(EE_OFFSET_IP + i);
        EEPROM.get(EE_OFFSET_FILTER, g_param.input_filter_ms);
        for (int i = 0; i < 4; i++)
            EEPROM.get(EE_OFFSET_THRESHOLD + i * 2, g_param.threshold[i]);
    } else {
        /* 首次启动或数据损坏，初始化默认值并写入 */
        param_init_defaults();
        param_save();
    }

    TRACE("P");
}

void param_save(void)
{
    EEPROM.update(EE_OFFSET_FLAG,    g_param.init_flag);
    EEPROM.update(EE_OFFSET_SLAVEID, g_param.slave_id);
    EEPROM.put(EE_OFFSET_BAUDRATE,   g_param.baudrate);
    for (int i = 0; i < 6; i++) EEPROM.update(EE_OFFSET_MAC + i, g_param.mac[i]);
    for (int i = 0; i < 4; i++) EEPROM.update(EE_OFFSET_IP + i,   g_param.ip[i]);
    EEPROM.put(EE_OFFSET_FILTER,     g_param.input_filter_ms);
    for (int i = 0; i < 4; i++)
        EEPROM.put(EE_OFFSET_THRESHOLD + i * 2, g_param.threshold[i]);

    DBG("PARAM", "saved to EEPROM");
}

void param_factory_reset(void)
{
    param_init_defaults();
    param_save();
    DBG("PARAM", "factory reset");
}
