/**
 * @file    hmi.h
 * @brief   淘晶驰VT串口屏通信模块
 */

#ifndef MOD_HMI_H_
#define MOD_HMI_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

/* ========================== HMI 协议常量 ========================== */
#define HMI_HEAD_TEMP         0x03
#define HMI_HEAD_HUMID        0x04
#define HMI_HEAD_CO2          0x05
#define HMI_HEAD_NH3          0x06
#define HMI_HEAD_NUMERIC      0x01
#define HMI_HEAD_PARAM_GROUP  0x02
#define HMI_HEAD_THRESHOLD_A  0x0A
#define HMI_HEAD_THRESHOLD_B  0x0B
#define HMI_HEAD_THRESHOLD_C  0x0C
#define HMI_HEAD_THRESHOLD_D  0x0D
#define HMI_HEAD_PARAM_E      0x0E
#define HMI_HEAD_PARAM_F      0x0F
#define HMI_HEAD_DEV_BASE     0x10
#define HMI_HEAD_DEV_END      0x80
#define HMI_HEAD_VT           0xEE   /**< VT 内部协议帧头，需过滤 */

void hmi_init(void);
void hmi_send(uint8_t flag, float value, uint8_t decimals);
void hmi_task(void *pvParameters);

#endif /* MOD_HMI_H_ */
