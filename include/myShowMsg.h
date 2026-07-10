#ifndef _my_ShowMsg_H_
#define _my_ShowMsg_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>

// 使用串口输出信息，若不使用则注释掉
#define UseSerialPrint

/**
 * @brief 显示消息
 *
 * @tparam T 要显示的数据类型
 * @param msg 要显示的数据
 * @param lineNow 是否换行
 */
template <typename T>
void ShowMsg(const T &msg, bool lineNow = false)
{
#ifdef UseSerialPrint
  lineNow ? Serial.println(msg) : Serial.print(msg);
#endif
}

#endif