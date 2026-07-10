/**
 * @file    g4g.h
 * @brief   4G 模块 - 占位 (USART6)
 */

#ifndef MOD_G4G_H_
#define MOD_G4G_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void g4g_init(void);
void g4g_task(void *pvParameters);

#endif /* MOD_G4G_H_ */
