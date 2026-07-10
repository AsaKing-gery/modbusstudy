/**
 * @file    tasks.h
 * @brief   FreeRTOS 任务管理
 */

#ifndef APP_TASKS_H_
#define APP_TASKS_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

void app_create_tasks(void *pvParameters);

#endif /* APP_TASKS_H_ */
