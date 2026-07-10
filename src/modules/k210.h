/**
 * @file    k210.h
 * @brief   K210 摄像头模块 - 占位 (USART2)
 */

#ifndef MOD_K210_H_
#define MOD_K210_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void k210_init(void);
void k210_task(void *pvParameters);

#endif /* MOD_K210_H_ */
