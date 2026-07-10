/**
 * @file    tasks.cpp
 * @brief   FreeRTOS 任务创建与管理
 * @note    每个任务功能独立，通过互斥锁和共享寄存器表通信
 */

#include "tasks.h"
#include "app_debug.h"
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
    bool led_state = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));

        /* 1Hz LED 闪烁 + 运行时间 */
        if (millis() - last_tick >= 1000) {
            last_tick = millis();
            led_state = !led_state;
            digitalWrite(PIN_RUN_LED, led_state ? HIGH : LOW);
            modbus_reg_set(REG_UPTIME, last_tick / 1000);
        }
    }
}

/* ========================== 任务创建工厂 ========================== */
void app_create_tasks(void *pvParameters)
{
    (void)pvParameters;

    xTaskCreate(task_watchdog,     "WDT",   TASK_STACK_WATCHDOG,    NULL, TASK_PRIO_WATCHDOG,    NULL);
    xTaskCreate(modbus_rtu_task,   "ModRTU",TASK_STACK_MODBUS,      NULL, TASK_PRIO_MODBUS,      NULL);
    xTaskCreate(task_main,         "Main",  TASK_STACK_MAIN,        NULL, TASK_PRIO_MAIN,        NULL);
    xTaskCreate(hmi_task,          "HMI",   TASK_STACK_HMI,         NULL, TASK_PRIO_HMI,         NULL);
    xTaskCreate(modbus_tcp_task,   "ModTCP",TASK_STACK_MODBUS_TCP,  NULL, TASK_PRIO_MODBUS_TCP,  NULL);

    /* 占位模块 - 按需启用 */
    // xTaskCreate(esp32_task,     "ESP32", 256, NULL, 2, NULL);
    // xTaskCreate(g4g_task,       "4G",    256, NULL, 2, NULL);
    // xTaskCreate(k210_task,      "K210",   256, NULL, 2, NULL);
    // xTaskCreate(lcd_task,       "LCD",   512,  NULL, 2, NULL);

    DBG("APP", "tasks created");
    vTaskDelete(NULL);
}
