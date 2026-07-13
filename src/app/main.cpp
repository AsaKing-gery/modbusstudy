/**
 * @file    main.cpp
 * @brief   系统入口 - 远程 I/O 主站 v4.05.01
 * @note    STM32F407VET6 / FreeRTOS / Arduino 框架
 *
 * 硬件接线清单:
 *   调试串口:    USART2 (PD5=TX, PD6=RX)
 *   串口屏 HMI:  SoftwareSerial (PA15=TX, PC10=RX)  @19200
 *   RS485:       USART1 (PA9=TX, PA10=RX, PC4=EN)  @115200
 *   ESP32:       SPI2  (PB13=SCK, PB14=MISO, PB15=MOSI, PB12=NSS)
 *   4G 模块:     USART6 (PA7=TX, PA6=RX) + PE6(RDY) + PE5(DTR) + PC11(RST)
 *   K210 摄像头: USART2 (PD5=TX, PD6=RX)  (与调试串口共用)
 *   SPI 屏幕:    SPI1  (PB3=SCK, PB5=MOSI)  + PE4(BLK) + PE3(CS) + PE2(DC)
 *   继电器:      8ch   (PE7~PE14)  低电平有效
 *   指示灯:      PE0=RUN(高亮), PE1=ERROR(高亮)
 *
 * FreeRTOS 任务:
 *   优先级6: 看门狗喂狗 (400ms 超时)
 *   优先级5: Modbus RTU 从站 (RS485)
 *   优先级3: 主业务 + HMI 串口屏接收
 *   优先级2: Modbus TCP (ESP32 WiFi 桥接, 占位)
 */

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "bsp/bsp_config.h"
#include "bsp/bsp_init.h"
#include "app/app_debug.h"
#include "app/param.h"
#include "app/tasks.h"
#include "drivers/relay.h"
#include "drivers/led.h"
#include "modbus/modbus_rtu.h"
#include "modbus/modbus_tcp.h"
#include "modules/hmi.h"
#include "modules/esp32.h"
#include "modules/lcd.h"

/* ========================== 系统启动 ========================== */

void setup(void)
{
    /* --- 阶段1: 调试串口 --- */
    DEBUG_SERIAL.begin(DEBUG_BAUDRATE);
    TRACE_LN("\n\nSystem Version:" FIRMWARE_VERSION_STR "  Remote IO Start...");

    /* --- 阶段2: 系统信息 --- */
    bsp_show_system_info();

    /* --- 阶段3: 加载参��� (EEPROM or defaults) --- */
    param_load();

    /* --- 阶段4: GPIO + 驱动初始化 --- */
    bsp_gpio_init();
    led_init();
    relay_init();

    /* --- 阶段5: 外设初始化 --- */
    TRACE("4");

    modbus_rtu_init();      /* USART1 + RS485 */
    modbus_tcp_init();      /* Stub */
    esp32_init();           /* SPI2 slave + IRQ */
    hmi_init();             /* SoftwareSerial + UART7 pins */
    /* lcd_init() deferred to lcd_task (after scheduler start) */

    TRACE_LN("OK");

    /* --- 阶段6: 创建任务 + 启动调度器 --- */
    xTaskCreate(app_create_tasks, "TaskInit", 256, NULL, 0, NULL);
    vTaskStartScheduler();

    /* 永远不会到达这里 */
    while (true) {}
}

void loop(void)
{
    /* FreeRTOS 接管，loop 不会执行 */
}

/* ========================== FreeRTOS 钩子 ========================== */

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    DEBUG_SERIAL.print("STACK OVERFLOW: ");
    DEBUG_SERIAL.println(pcTaskName);
    while (true) {}
}

extern "C" void vApplicationMallocFailedHook(void)
{
    DEBUG_SERIAL.println("MALLOC FAILED");
    while (true) {}
}
