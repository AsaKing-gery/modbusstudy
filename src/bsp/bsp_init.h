/**
 * @file    bsp_init.h
 * @brief   板级初始化接口
 */

#ifndef BSP_INIT_H_
#define BSP_INIT_H_

#include <Arduino.h>

uint32_t bsp_get_mcu_id(uint8_t offset_index);
void     bsp_show_system_info(void);
void     bsp_gpio_init(void);

#endif /* BSP_INIT_H_ */
