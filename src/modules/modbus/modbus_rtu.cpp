/**
 * @file    modbus_rtu.cpp
 * @brief   Modbus RTU 从站 - 基于 Modbus-Serial 库
 * @note    USART1 + RS485 (PA9/PA10/PC4)
 */

#include "modbus_rtu.h"
#include "modbus_core.h"
#include "bsp/bsp_debug.h"
#include <ModbusSerial.h>

/* ========================== Modbus RTU 实例 ========================== */
/** ModbusSerial 构造函数: (串口流, 从站ID, TX使能引脚) */
static ModbusSerial mb(RS485_SERIAL, 1, PIN_RS485_EN);

/* 上次输出状态缓存，用于检测外部写入变化 */
static uint16_t prev_output = 0;

/* ========================== 初始化 ========================== */
void modbus_rtu_init(void)
{
    TRACE("M");

    /* 配置波特率 */
    RS485_SERIAL.begin(g_param.baudrate, RS485_CONFIG);
    mb.config(g_param.baudrate);

    /* 初始化共享寄存器表 */
    modbus_regs_init();

    /* 注册保持寄存器块 */
    for (uint8_t i = 0; i < REG_COUNT; i++) {
        mb.addHreg(i, modbus_reg_get(i));
    }

    /* 同步输出状态 */
    prev_output = modbus_reg_get(REG_OUTPUT_STATE);

    TRACE("m");
}

/* ========================== 从 ModbusSerial 同步寄存器变化到共享表 ========================== */
static void sync_registers_from_mb(void)
{
    /* 检测哪些可写寄存器被 Modbus Master 修改了 */
    uint16_t new_val;

    new_val = mb.Hreg(REG_SLAVE_ID);
    if (new_val != modbus_reg_get(REG_SLAVE_ID)) modbus_reg_set(REG_SLAVE_ID, new_val);

    new_val = mb.Hreg(REG_BAUDRATE);
    if (new_val != modbus_reg_get(REG_BAUDRATE)) modbus_reg_set(REG_BAUDRATE, new_val);

    new_val = mb.Hreg(REG_MAC_LO);
    if (new_val != modbus_reg_get(REG_MAC_LO)) modbus_reg_set(REG_MAC_LO, new_val);

    new_val = mb.Hreg(REG_MAC_MID);
    if (new_val != modbus_reg_get(REG_MAC_MID)) modbus_reg_set(REG_MAC_MID, new_val);

    new_val = mb.Hreg(REG_MAC_HI);
    if (new_val != modbus_reg_get(REG_MAC_HI)) modbus_reg_set(REG_MAC_HI, new_val);

    new_val = mb.Hreg(REG_IP_PART1);
    if (new_val != modbus_reg_get(REG_IP_PART1)) modbus_reg_set(REG_IP_PART1, new_val);

    new_val = mb.Hreg(REG_IP_PART2);
    if (new_val != modbus_reg_get(REG_IP_PART2)) modbus_reg_set(REG_IP_PART2, new_val);

    new_val = mb.Hreg(REG_INPUT_FILTER);
    if (new_val != modbus_reg_get(REG_INPUT_FILTER)) modbus_reg_set(REG_INPUT_FILTER, new_val);

    new_val = mb.Hreg(REG_OUTPUT_STATE);
    if (new_val != modbus_reg_get(REG_OUTPUT_STATE)) modbus_reg_set(REG_OUTPUT_STATE, new_val);

    new_val = mb.Hreg(REG_THRESHOLD_A);
    if (new_val != modbus_reg_get(REG_THRESHOLD_A)) modbus_reg_set(REG_THRESHOLD_A, new_val);

    new_val = mb.Hreg(REG_THRESHOLD_B);
    if (new_val != modbus_reg_get(REG_THRESHOLD_B)) modbus_reg_set(REG_THRESHOLD_B, new_val);

    new_val = mb.Hreg(REG_THRESHOLD_C);
    if (new_val != modbus_reg_get(REG_THRESHOLD_C)) modbus_reg_set(REG_THRESHOLD_C, new_val);

    new_val = mb.Hreg(REG_THRESHOLD_D);
    if (new_val != modbus_reg_get(REG_THRESHOLD_D)) modbus_reg_set(REG_THRESHOLD_D, new_val);
}

/* ========================== 从共享表同步到 ModbusSerial ========================== */
static void sync_registers_to_mb(void)
{
    /* 同步只读寄存器到 ModbusSerial (写保护) */
    mb.setHreg(REG_VERSION,      FIRMWARE_VERSION);
    mb.setHreg(REG_UPTIME,       modbus_reg_get(REG_UPTIME));

    /* 推送 HMI 可能修改的可写寄存器，保持 mb 内部与共享表一致 */
    mb.setHreg(REG_OUTPUT_STATE, modbus_reg_get(REG_OUTPUT_STATE));
    mb.setHreg(REG_THRESHOLD_A,  modbus_reg_get(REG_THRESHOLD_A));
    mb.setHreg(REG_THRESHOLD_B,  modbus_reg_get(REG_THRESHOLD_B));
    mb.setHreg(REG_THRESHOLD_C,  modbus_reg_get(REG_THRESHOLD_C));
    mb.setHreg(REG_THRESHOLD_D,  modbus_reg_get(REG_THRESHOLD_D));

    /* ESP32 传感器数据 (只读，保护不被 Modbus Master 覆写) */
    mb.setHreg(REG_TEMP_X100,    modbus_reg_get(REG_TEMP_X100));
    mb.setHreg(REG_HUMI_X100,    modbus_reg_get(REG_HUMI_X100));
    mb.setHreg(REG_CO2,          modbus_reg_get(REG_CO2));
    mb.setHreg(REG_NH3_X100,     modbus_reg_get(REG_NH3_X100));
    mb.setHreg(REG_LUX_X100,     modbus_reg_get(REG_LUX_X100));
    mb.setHreg(REG_ESP32_STATUS, modbus_reg_get(REG_ESP32_STATUS));
}

/* ========================== 任务循环 ========================== */
void modbus_rtu_task(void *pvParameters)
{
    (void)pvParameters;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1));

        if (xSemaphoreTake(g_rs485_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            /* 更新 ModbusSerial 中的只读寄存器 */
            sync_registers_to_mb();

            /* 处理 Modbus 通信 */
            mb.task();

            /* 同步可能被写入的寄存器 */
            sync_registers_from_mb();

            /* 输出到继电器 */
            uint16_t output = modbus_reg_get(REG_OUTPUT_STATE);
            if (output != prev_output) {
                if (g_modbus_output_cb) {
                    g_modbus_output_cb(output & 0xFF);
                }
                prev_output = output;
            }

            /* 处理命令 */
            modbus_handle_command();

            /* 保护只读寄存器不被外部写入覆盖 */
            mb.setHreg(REG_VERSION, FIRMWARE_VERSION);

            xSemaphoreGive(g_rs485_mutex);
        }
    }
}
