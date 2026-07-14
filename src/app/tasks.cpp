/**
 * @file    tasks.cpp
 * @brief   FreeRTOS 任务创建与管理
 * @note    每个任务功能独立，通过互斥锁和共享寄存器表通信
 */

#include "tasks.h"
#include "bsp/bsp_debug.h"
#include "drivers/led.h"
#include "drivers/relay.h"
#include "modbus/modbus_rtu.h"
#include "modbus/modbus_tcp.h"
#include "modbus/modbus_core.h"
#include "modules/hmi.h"
#include "modules/esp32.h"
#include "modules/g4g.h"
#include "modules/k210.h"
#include "modules/lcd.h"
#include <IWatchdog.h>

/* ========================== 看门狗任务 ========================== */
static void task_watchdog(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    DBG("WDT", "started");

    IWatchdog.begin(1000 * WDT_TIMEOUT_MS);
    uint32_t last_feed = millis();
    bool reset_flag = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (millis() - last_feed > WDT_TIMEOUT_MS / 2) {
            IWatchdog.reload();
            last_feed = millis();
        }

        if (IWatchdog.isReset()) {
            reset_flag = true;
            IWatchdog.clearReset();
        }

        if (reset_flag) {
            led_error_on();
        }
    }
}

/* ========================== 主业务任务 ========================== */
static void task_main(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    DBG("MAIN", "started");

    uint32_t last_tick = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));

        /* 1Hz LED 闪烁 + 运行时间 + 传感器阈值自动控制 */
        if (millis() - last_tick >= 1000) {
            last_tick = millis();
            led_run_toggle();
            modbus_reg_set(REG_UPTIME, last_tick / 1000);

            /* Page3 阈值已写入 g_regs(RAM), 不自动写 EEPROM.
             * 需要持久化时通过 Modbus CMD_SAVE(0x0A) 手动保存 */

            /* ─── 传感器阈值自动控制 (4路风机 + 4路加湿器) ─── */
            uint16_t status = modbus_reg_get(REG_ESP32_STATUS);

            /* 诊断心跳: 每30s打印一次状态 */
            static uint32_t last_diag = 0;
            if (millis() - last_diag >= 30000) {
                last_diag = millis();
                DEBUG_SERIAL.print("[MAIN.DIAG] status=0x"); DEBUG_SERIAL.print(status, HEX);
                DEBUG_SERIAL.print(" output=0x"); DEBUG_SERIAL.print(modbus_reg_get(REG_OUTPUT_STATE), HEX);
                DEBUG_SERIAL.print(" T="); DEBUG_SERIAL.print(modbus_reg_get(REG_TEMP_X100));
                DEBUG_SERIAL.print(" H="); DEBUG_SERIAL.print(modbus_reg_get(REG_HUMI_X100));
                DEBUG_SERIAL.print(" CO2="); DEBUG_SERIAL.print(modbus_reg_get(REG_CO2));
                DEBUG_SERIAL.print(" FAN:"); DEBUG_SERIAL.print((modbus_reg_get(REG_OUTPUT_STATE) & 0xF0) ? "ON" : "OFF");
                DEBUG_SERIAL.println();
                DEBUG_SERIAL.flush();
            }

            if (status & 0x02) {
                int16_t temp = (int16_t)modbus_reg_get(REG_TEMP_X100);
                int16_t humi = (int16_t)modbus_reg_get(REG_HUMI_X100);
                uint16_t co2 = modbus_reg_get(REG_CO2);
                int16_t nh3  = (int16_t)modbus_reg_get(REG_NH3_X100);

                uint16_t t_hi = modbus_reg_get(REG_TEMP_HI_X100);
                uint16_t t_lo = modbus_reg_get(REG_TEMP_LO_X100);
                uint16_t h_hi = modbus_reg_get(REG_HUMI_HI_X100);
                uint16_t h_lo = modbus_reg_get(REG_HUMI_LO_X100);
                uint16_t c_hi = modbus_reg_get(REG_CO2_HI);
                uint16_t c_lo = modbus_reg_get(REG_CO2_LO);
                uint16_t n_hi = modbus_reg_get(REG_NH3_HI_X100);
                uint16_t n_lo = modbus_reg_get(REG_NH3_LO_X100);

                DEBUG_SERIAL.print("[AUTO] thresh: T("); DEBUG_SERIAL.print(t_hi);
                DEBUG_SERIAL.print("/"); DEBUG_SERIAL.print(t_lo);
                DEBUG_SERIAL.print(") H("); DEBUG_SERIAL.print(h_hi);
                DEBUG_SERIAL.print("/"); DEBUG_SERIAL.print(h_lo);
                DEBUG_SERIAL.print(") C("); DEBUG_SERIAL.print(c_hi);
                DEBUG_SERIAL.print("/"); DEBUG_SERIAL.print(c_lo);
                DEBUG_SERIAL.print(") N("); DEBUG_SERIAL.print(n_hi);
                DEBUG_SERIAL.print("/"); DEBUG_SERIAL.print(n_lo);
                DEBUG_SERIAL.print(") output=0x"); DEBUG_SERIAL.print(modbus_reg_get(REG_OUTPUT_STATE), HEX);
                DEBUG_SERIAL.println();
                DEBUG_SERIAL.flush();

                /* 阈值全为 0 或 0xFFFF(未编程EEPROM) → 纯手动模式 */
                bool auto_enabled = ((t_hi && t_hi != 0xFFFF) || (t_lo && t_lo != 0xFFFF)
                                  || (h_hi && h_hi != 0xFFFF) || (h_lo && h_lo != 0xFFFF)
                                  || (c_hi && c_hi != 0xFFFF) || (c_lo && c_lo != 0xFFFF)
                                  || (n_hi && n_hi != 0xFFFF) || (n_lo && n_lo != 0xFFFF));

                if (auto_enabled) {
                    /* 手动模式优先: 不覆盖用户操作 */
                    if (!hmi_is_manual_mode()) {
                        bool fan_on       = false;  /* Y5-Y8 (bit 4-7) */
                        bool humid_on     = false;  /* Y1-Y4 (bit 0-3) */

                        /* 温度: 过高→风机降温, 过低→加湿器暖雾升温 */
                        if (t_hi > 0 && t_hi != 0xFFFF && temp > (int16_t)t_hi) fan_on   = true;
                        if (t_lo > 0 && t_lo != 0xFFFF && temp < (int16_t)t_lo) humid_on = true;

                        /* 湿度: 过高→风机除湿, 过低→加湿器加湿 */
                        if (h_hi > 0 && h_hi != 0xFFFF && humi > (int16_t)h_hi) fan_on   = true;
                        if (h_lo > 0 && h_lo != 0xFFFF && humi < (int16_t)h_lo) humid_on = true;

                        /* CO2: 过高→风机通风 */
                        if (c_hi > 0 && c_hi != 0xFFFF && co2 > c_hi) fan_on = true;

                        /* NH3: 过高→风机通风 */
                        if (n_hi > 0 && n_hi != 0xFFFF && nh3 > (int16_t)n_hi) fan_on = true;

                        uint16_t state = modbus_reg_get(REG_OUTPUT_STATE);
                        if (fan_on)   state |= 0xF0;     /* 开4路风机 */
                        else          state &= ~0xF0;
                        if (humid_on) state |= 0x0F;     /* 开4路加湿器 */
                        else          state &= ~0x0F;

                        modbus_reg_set(REG_OUTPUT_STATE, state);
                        relay_set_all(state & 0xFF);
                    }
                }
            }
        }
    }
}

/* ========================== 任务创建工厂 ========================== */
void app_create_tasks(void *pvParameters)
{
    (void)pvParameters;

    DBG("APP", "creating tasks...");

    DEBUG_SERIAL.print("[APP] 1/7 WDT..."); DEBUG_SERIAL.flush();
    xTaskCreate(task_watchdog,     "WDT",   TASK_STACK_WATCHDOG,    NULL, TASK_PRIO_WATCHDOG,    NULL);

    DEBUG_SERIAL.print("[APP] 2/7 ModRTU..."); DEBUG_SERIAL.flush();
    xTaskCreate(modbus_rtu_task,   "ModRTU",TASK_STACK_MODBUS,      NULL, TASK_PRIO_MODBUS,      NULL);

    DEBUG_SERIAL.print("[APP] 3/7 Main..."); DEBUG_SERIAL.flush();
    xTaskCreate(task_main,         "Main",  TASK_STACK_MAIN,        NULL, TASK_PRIO_MAIN,        NULL);

    DEBUG_SERIAL.print("[APP] 4/7 HMI..."); DEBUG_SERIAL.flush();
    xTaskCreate(hmi_task,          "HMI",   TASK_STACK_HMI,         NULL, TASK_PRIO_HMI,         NULL);

    DEBUG_SERIAL.print("[APP] 5/7 ESP32..."); DEBUG_SERIAL.flush();
    xTaskCreate(esp32_task,        "ESP32", TASK_STACK_ESP32,       NULL, TASK_PRIO_ESP32,       NULL);

    DEBUG_SERIAL.print("[APP] 6/7 ModTCP..."); DEBUG_SERIAL.flush();
    xTaskCreate(modbus_tcp_task,   "ModTCP",TASK_STACK_MODBUS_TCP,  NULL, TASK_PRIO_MODBUS_TCP,  NULL);

    DEBUG_SERIAL.print("[APP] 7/7 LCD..."); DEBUG_SERIAL.flush();
    xTaskCreate(lcd_task,         "LCD",   TASK_STACK_LCD,         NULL, TASK_PRIO_LCD,         NULL);

    /* 占位模块 - 按需启用 */
    // xTaskCreate(g4g_task,       "4G",    256, NULL, 2, NULL);
    // xTaskCreate(k210_task,      "K210",   256, NULL, 2, NULL);

    DEBUG_SERIAL.println("[APP] DONE");
    DEBUG_SERIAL.flush();
    vTaskDelete(NULL);
}
