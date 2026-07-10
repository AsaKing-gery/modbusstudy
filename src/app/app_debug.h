/**
 * @file    app_debug.h
 * @brief   统一调试输出宏
 * @note    使用裸 Serial.print 避免 String 分配和阻塞问题
 */

#ifndef APP_DEBUG_H_
#define APP_DEBUG_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== 启动追踪标记（仅 setup 阶段使用） ========================== */
#define TRACE(c) do { DEBUG_SERIAL.print(c); DEBUG_SERIAL.flush(); } while(0)
#define TRACE_LN(s) do { DEBUG_SERIAL.println(s); DEBUG_SERIAL.flush(); } while(0)

/* ========================== 运行时调试开关 ========================== */
/** 设为 0 关闭所有运行时调试输出，减小固件体积 */
#define DEBUG_ENABLE 1

#if DEBUG_ENABLE
    #define DBG(tag, msg) do { \
        DEBUG_SERIAL.print("["); DEBUG_SERIAL.print(tag); \
        DEBUG_SERIAL.print("] "); DEBUG_SERIAL.println(msg); \
    } while(0)

    #define DBG_FMT(tag, fmt, ...) do { \
        DEBUG_SERIAL.print("["); DEBUG_SERIAL.print(tag); \
        DEBUG_SERIAL.print("] "); DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); \
        DEBUG_SERIAL.println(); \
    } while(0)
#else
    #define DBG(tag, msg)       ((void)0)
    #define DBG_FMT(tag, ...)   ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_DEBUG_H_ */
