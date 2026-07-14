/**
 * @file    bsp_config.h
 * @brief   板级支持包 - 引脚定义与外设映射
 * @note    所有硬件相关定义集中于此，修改硬件时只需改此文件
 */

#ifndef BSP_CONFIG_H_
#define BSP_CONFIG_H_

#include <Arduino.h>

/* ========================== 系统时钟 ========================== */
#define SYSCLK_FREQ_HZ      168000000UL

/* ========================== 调试串口 (USART2: PD5=TX, PD6=RX) ========================== */
/** 调试串口：USART2，与 K210 共享物理引脚，调试期间 K210 禁用 */
extern HardwareSerial DebugSerial;
#define DEBUG_SERIAL         DebugSerial
#define DEBUG_BAUDRATE       115200

/* ========================== 运行指示灯 ========================== */
#define PIN_RUN_LED          PE0     /**< 运行指示灯，低电平点亮 */
#define PIN_ERROR_LED        PE1     /**< 错误指示灯，低电平点亮 */
#define LED_ACTIVE           LOW     /**< LED 有效电平 */

/* ========================== 继电器驱动 (8路) ========================== */
/** Y1~Y4: 加湿器，Y5~Y8: 风机，低电平有效（继电器闭合） */
#define PIN_RELAY_BASE       PE7
#define PIN_RELAY_COUNT      8

#define PIN_HUMIDIFIER_1     PE7
#define PIN_HUMIDIFIER_2     PE8
#define PIN_HUMIDIFIER_3     PE9
#define PIN_HUMIDIFIER_4     PE10
#define PIN_FAN_1            PE11
#define PIN_FAN_2            PE12
#define PIN_FAN_3            PE13
#define PIN_FAN_4            PE14

/* ========================== RS485 (Modbus RTU) ========================== */
extern HardwareSerial RS485_SERIAL;                 /**< USART1 实例 */
#define RS485_BAUDRATE       115200
#define RS485_CONFIG         SERIAL_8N1
#define PIN_RS485_TX         PA9                 /**< USART1_TX */
#define PIN_RS485_RX         PA10                /**< USART1_RX */
#define PIN_RS485_EN         PC4                 /**< RS485 收发使能，HIGH=发送 */

/* ========================== 串口屏 HMI (淘晶驰VT系列) ========================== */
#define HMI_SERIAL           HMISerial           /**< SoftwareSerial 实例 */
#define HMI_BAUDRATE         19200
#define PIN_HMI_TX           PA15                /**< UART7_TX */
#define PIN_HMI_RX           PC10                /**< UART7_RX */

/* ========================== ESP32 (SPI 从机 - 1MHz MODE0) ========================== */
#define ESP32_SPI            SPI2
#define ESP32_SPI_AF         GPIO_AF5_SPI2
#define ESP32_SPI_IRQn       SPI2_IRQn
#define ESP32_SPI_FRAME_LEN  14       /**< 固定帧长度 14 字节 */
#define PIN_ESP32_SCK        PB13     /**< SPI2_SCK  (AF5) */
#define PIN_ESP32_MISO       PB14     /**< SPI2_MISO (AF5) */
#define PIN_ESP32_MOSI       PB15     /**< SPI2_MOSI (AF5) */
#define PIN_ESP32_NSS        PB12     /**< SPI2_NSS  (AF5, 硬件NSS) */

/* ========================== 4G 模块 (USART6) ========================== */
#define G4G_SERIAL           Serial6             /**< USART6 */
#define G4G_BAUDRATE         115200
#define PIN_G4G_TX           PA7                 /**< USART6_TX */
#define PIN_G4G_RX           PA6                 /**< USART6_RX */
#define PIN_G4G_RDY          PE6                 /**< 模块就绪状态输入 */
#define PIN_G4G_DTR          PE5                 /**< 数据终端就绪输出 */
#define PIN_G4G_RST          PC11                /**< 模块复位输出 */

/* ========================== K210 摄像头 (USART2 - 调试复用，当前禁用) ========================== */
#define K210_SERIAL          Serial2             /**< USART2 (与调试串口共用 PD5/PD6) */
#define K210_BAUDRATE        115200
#define PIN_K210_TX          PD5                 /**< USART2_TX */
#define PIN_K210_RX          PD6                 /**< USART2_RX */

/* ========================== SPI 屏幕 (LVGL / SPI1 只写) ========================== */
#define LCD_SPI              SPI1
#define PIN_LCD_SCK          PB3                 /**< SPI1_SCK */
#define PIN_LCD_MOSI         PB5                 /**< SPI1_MOSI (只写，无需 MISO) */
#define PIN_LCD_BLK          PE4                 /**< 背光，可 PWM 调光 */
#define PIN_LCD_CS           PE3                 /**< 片选 */
#define PIN_LCD_DC           PE2                 /**< 数据/命令选择 */

/* ========================== 调试接口 ========================== */
#define PIN_SWD_CLK          PA14
#define PIN_SWD_DIO          PA13

/* ========================== 看门狗 ========================== */
#define WDT_TIMEOUT_MS       2000                /**< IWDG 超时时间 (需覆盖Flash扇区擦除~1s) */

/* ========================== FreeRTOS ========================== */
#define TASK_STACK_WATCHDOG    96
#define TASK_STACK_MODBUS      128
#define TASK_STACK_MAIN        256
#define TASK_STACK_HMI         768     /**< 复合帧 vsnprintf + debug 打印 需大栈 */
#define TASK_STACK_MODBUS_TCP  256
#define TASK_STACK_LCD          768
#define TASK_STACK_ESP32       768

#define TASK_PRIO_WATCHDOG     6
#define TASK_PRIO_MODBUS       5
#define TASK_PRIO_MAIN         3
#define TASK_PRIO_HMI          4
#define TASK_PRIO_MODBUS_TCP   2
#define TASK_PRIO_LCD          3
#define TASK_PRIO_ESP32        2

/* ========================== Modbus 寄存器地址定义 ========================== */
/** 保持寄存器 (Holding Registers) */
enum ModbusRegister
{
    /* 参数区 (0-9, 部分可写 EEPROM) */
    REG_VERSION          = 0,    /**< R    固件版本号 */
    REG_SLAVE_ID         = 1,    /**< R/W  从站地址 (EEPROM) */
    REG_BAUDRATE         = 2,    /**< R/W  RS485 波特率 (EEPROM) */
    REG_COMMAND          = 3,    /**< R/W  参数命令: 10=保存 20=重载 30=重启 66=恢复出厂 */
    REG_MAC_LO           = 4,    /**< R/W  MAC 低16位 (EEPROM) */
    REG_MAC_MID          = 5,    /**< R/W  MAC 中16位 (EEPROM) */
    REG_MAC_HI           = 6,    /**< R/W  MAC 高16位 (EEPROM) */
    REG_IP_PART1         = 7,    /**< R/W  IP 高16位 [192,168] (EEPROM) */
    REG_IP_PART2         = 8,    /**< R/W  IP 低16位 [1,168]  (EEPROM) */
    REG_INPUT_FILTER     = 9,    /**< R/W  输入滤波时间 ms (EEPROM) */

    /* 运行数据区 (10-15) */
    REG_UPTIME           = 10,   /**< R    系统运行时间 秒 */
    REG_OUTPUT_STATE     = 11,   /**< R/W  8路继电器输出状态 bit0~7 */
    REG_THRESHOLD_A      = 12,   /**< R/W  阈值A (EEPROM) */
    REG_THRESHOLD_B      = 13,   /**< R/W  阈值B (EEPROM) */
    REG_THRESHOLD_C      = 14,   /**< R/W  阈值C (EEPROM) */
    REG_THRESHOLD_D      = 15,   /**< R/W  阈值D (EEPROM) */

    /* ESP32 传感器数据区 (16-21, 只读: 来自云端/传感器) */
    REG_TEMP_X100        = 16,   /**< R    温度 ×100 °C (int16) */
    REG_HUMI_X100        = 17,   /**< R    湿度 ×100 %  (int16) */
    REG_CO2              = 18,   /**< R    CO2 ppm         (int16) */
    REG_NH3_X100         = 19,   /**< R    NH3 ×100 ppm   (int16) */
    REG_LUX_X100         = 20,   /**< R    光照 ×100 lux  (int16) */
    REG_ESP32_STATUS     = 21,   /**< R    bit0=已连接 bit1=帧有效 bit2-7=运行帧数 */

    /* 传感器阈值区 (22-29, R/W EEPROM, 页面3刷新) */
    REG_TEMP_HI_X100     = 22,   /**< R/W  温度上限 ×100 (页面3-控件26上半) */
    REG_TEMP_LO_X100     = 23,   /**< R/W  温度下限 ×100 (页面3-控件26下半) */
    REG_HUMI_HI_X100     = 24,   /**< R/W  湿度上限 ×100 (页面3-控件27上半) */
    REG_HUMI_LO_X100     = 25,   /**< R/W  湿度下限 ×100 (页面3-控件27下半) */
    REG_CO2_HI           = 26,   /**< R/W  CO2上限 ppm   (页面3-控件28上半) */
    REG_CO2_LO           = 27,   /**< R/W  CO2下限 ppm   (页面3-控件28下半) */
    REG_NH3_HI_X100      = 28,   /**< R/W  NH3上限 ×100  (页面3-控件29上半) */
    REG_NH3_LO_X100      = 29,   /**< R/W  NH3下限 ×100  (页面3-控件29下半) */

    /* --- OTA 固件升级 (地址 30-34) --- */
    REG_OTA_STATUS       = 30,   /**< R    OTA 状态: 0=空闲 1=检查中 2=下载中 3=成功 4=失败 */
    REG_OTA_PROGRESS     = 31,   /**< R    OTA 下载进度 0-100 */
    REG_OTA_ERROR        = 32,   /**< R    OTA 错误码: 0=无 1=网络 2=签名 3=Flash 4=超时 */
    REG_OTA_VERSION      = 33,   /**< R    当前固件版本号 */
    REG_OTA_TRIGGER      = 34,   /**< W    写 1 手动触发 OTA 检查 */

    REG_COUNT            = 35    /**<       寄存器总数 */
};

#endif /* BSP_CONFIG_H_ */
