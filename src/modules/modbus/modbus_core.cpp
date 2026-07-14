/**
 * @file    modbus_core.cpp
 * @brief   Modbus 核心实现 - 寄存器表 + 参数同步
 * @note    RTU 和 TCP 共享同一个寄存器数组，通过互斥锁保护
 */

#include "modbus_core.h"
#include "modbus_rtu.h"
#include "bsp/bsp_debug.h"

/* ========================== 回调指针 ========================== */
modbus_output_cb_t g_modbus_output_cb = NULL;
modbus_param_save_cb_t g_modbus_param_save_cb = NULL;
modbus_factory_reset_cb_t g_modbus_factory_reset_cb = NULL;

/* ========================== 全局寄存器表 ========================== */
static uint16_t g_regs[REG_COUNT];

/* ========================== RS485 总线互斥锁 ========================== */
SemaphoreHandle_t g_rs485_mutex = NULL;

/* ========================== 初始化 ========================== */
void modbus_regs_init(void)
{
    memset(g_regs, 0, sizeof(g_regs));

    g_rs485_mutex = xSemaphoreCreateMutex();
    if (g_rs485_mutex == NULL) {
        DBG("MODBUS", "mutex create failed");
    }

    modbus_load_from_param();
}

/* ========================== 寄存器访问 ========================== */
uint16_t modbus_reg_get(uint8_t addr)
{
    if (addr >= REG_COUNT) return 0;
    return g_regs[addr];
}

void modbus_reg_set(uint8_t addr, uint16_t value)
{
    if (addr >= REG_COUNT) return;
    g_regs[addr] = value;
}

/* ========================== 参数同步 ========================== */
void modbus_load_from_param(void)
{
    g_regs[REG_VERSION]      = FIRMWARE_VERSION;
    g_regs[REG_SLAVE_ID]     = g_param.slave_id;
    g_regs[REG_BAUDRATE]     = (uint16_t)g_param.baudrate;
    g_regs[REG_MAC_LO]       = ((uint16_t)g_param.mac[1] << 8) | g_param.mac[0];
    g_regs[REG_MAC_MID]      = ((uint16_t)g_param.mac[3] << 8) | g_param.mac[2];
    g_regs[REG_MAC_HI]       = ((uint16_t)g_param.mac[5] << 8) | g_param.mac[4];
    g_regs[REG_IP_PART1]     = ((uint16_t)g_param.ip[0] << 8) | g_param.ip[1];
    g_regs[REG_IP_PART2]     = ((uint16_t)g_param.ip[2] << 8) | g_param.ip[3];
    g_regs[REG_INPUT_FILTER] = g_param.input_filter_ms;
    g_regs[REG_THRESHOLD_A]  = g_param.threshold[0];
    g_regs[REG_THRESHOLD_B]  = g_param.threshold[1];
    g_regs[REG_THRESHOLD_C]  = g_param.threshold[2];
    g_regs[REG_THRESHOLD_D]  = g_param.threshold[3];
    /* 页面3传感器阈值 */
    g_regs[REG_TEMP_HI_X100] = g_param.temp_hi_x100;
    g_regs[REG_TEMP_LO_X100] = g_param.temp_lo_x100;
    g_regs[REG_HUMI_HI_X100] = g_param.humi_hi_x100;
    g_regs[REG_HUMI_LO_X100] = g_param.humi_lo_x100;
    g_regs[REG_CO2_HI]       = g_param.co2_hi;
    g_regs[REG_CO2_LO]       = g_param.co2_lo;
    g_regs[REG_NH3_HI_X100]  = g_param.nh3_hi_x100;
    g_regs[REG_NH3_LO_X100]  = g_param.nh3_lo_x100;
    /* REG_COMMAND, REG_UPTIME, REG_OUTPUT_STATE 为运行时值 */
}

void modbus_save_to_param(void)
{
    g_param.slave_id  = g_regs[REG_SLAVE_ID] & 0xFF;
    g_param.baudrate  = g_regs[REG_BAUDRATE];
    g_param.mac[0]    = g_regs[REG_MAC_LO] & 0xFF;
    g_param.mac[1]    = (g_regs[REG_MAC_LO] >> 8) & 0xFF;
    g_param.mac[2]    = g_regs[REG_MAC_MID] & 0xFF;
    g_param.mac[3]    = (g_regs[REG_MAC_MID] >> 8) & 0xFF;
    g_param.mac[4]    = g_regs[REG_MAC_HI] & 0xFF;
    g_param.mac[5]    = (g_regs[REG_MAC_HI] >> 8) & 0xFF;
    g_param.ip[0]     = (g_regs[REG_IP_PART1] >> 8) & 0xFF;
    g_param.ip[1]     = g_regs[REG_IP_PART1] & 0xFF;
    g_param.ip[2]     = (g_regs[REG_IP_PART2] >> 8) & 0xFF;
    g_param.ip[3]     = g_regs[REG_IP_PART2] & 0xFF;
    g_param.input_filter_ms = g_regs[REG_INPUT_FILTER];
    g_param.threshold[0] = g_regs[REG_THRESHOLD_A];
    g_param.threshold[1] = g_regs[REG_THRESHOLD_B];
    g_param.threshold[2] = g_regs[REG_THRESHOLD_C];
    g_param.threshold[3] = g_regs[REG_THRESHOLD_D];
    /* 页面3传感器阈值 */
    g_param.temp_hi_x100 = g_regs[REG_TEMP_HI_X100];
    g_param.temp_lo_x100 = g_regs[REG_TEMP_LO_X100];
    g_param.humi_hi_x100 = g_regs[REG_HUMI_HI_X100];
    g_param.humi_lo_x100 = g_regs[REG_HUMI_LO_X100];
    g_param.co2_hi       = g_regs[REG_CO2_HI];
    g_param.co2_lo       = g_regs[REG_CO2_LO];
    g_param.nh3_hi_x100  = g_regs[REG_NH3_HI_X100];
    g_param.nh3_lo_x100  = g_regs[REG_NH3_LO_X100];

    if (g_modbus_param_save_cb) {
        g_modbus_param_save_cb();
    }
}

void modbus_handle_command(void)
{
    uint16_t cmd = g_regs[REG_COMMAND];
    if (cmd == 0) return;

    g_regs[REG_COMMAND] = 0;  /* 清除命令 */

    switch (cmd) {
    case CMD_SAVE:
        modbus_save_to_param();
        break;
    case CMD_RELOAD:
        modbus_load_from_param();
        break;
    case CMD_REBOOT:
        DBG("MODBUS", "rebooting...");
        vTaskDelay(pdMS_TO_TICKS(100));
        NVIC_SystemReset();
        break;
    case CMD_FACTORY_RESET:
        DBG("MODBUS", "factory reset...");
        if (g_modbus_factory_reset_cb) {
            g_modbus_factory_reset_cb();
        }
        modbus_load_from_param();
        vTaskDelay(pdMS_TO_TICKS(100));
        NVIC_SystemReset();
        break;
    default:
        break;
    }
}
