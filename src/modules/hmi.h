/**
 * @file    hmi.h
 * @brief   淘晶驰VT串口屏通信模块
 */

#ifndef MOD_HMI_H_
#define MOD_HMI_H_

#include <Arduino.h>
#include "bsp/bsp_config.h"

/* ========================== HMI 协议常量 ========================== */
/* 传感器数据显示帧头: 0xA1~0xA4 (避开 VT 底层拦截的 ASCII 控制字符 0x03~0x06) */
#define HMI_HEAD_TEMP         0xA1
#define HMI_HEAD_HUMID        0xA2
#define HMI_HEAD_NH3          0xA3
#define HMI_HEAD_CO2          0xA4
#define HMI_HEAD_COMPOUND     0xA5   /**< 复合帧: 4个传感器值逗号拼接, 一次发送避免分帧 */
#define HMI_HEAD_HISTORY1     0x07
#define HMI_HEAD_HISTORY2     0x08
#define HMI_HEAD_HISTORY3     0x09
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

/* 自由协议帧间隔 >=30ms, 防止 HMI 粘帧 (MD 文档 §5.1) */
#define HMI_FRAME_GAP_MS      30

/* ========================== 模拟调试模式 ========================== */
/**
 * 设为 1 开启模拟: 温湿度/CO2/NH3 全部写死 30.0, 并写入 Modbus 寄存器供自动控制测试
 * 设为 0 关闭模拟，使用真实 ESP32 数据
 */
#define HMI_SIM_ENABLE        0

void hmi_init(void);
void hmi_send(uint8_t flag, float value, uint8_t decimals);
void hmi_task(void *pvParameters);

/* 延迟保存: Page3 阈值变更后设标志, Main 任务统一写 EEPROM */
void hmi_mark_save_pending(void);
bool hmi_is_save_pending(void);

/* 传感器数据发送 (自由协议帧: [1-byte head][ASCII string]) */
void hmi_send_temperature(float temp_c);
void hmi_send_humidity(float humi_pct);
void hmi_send_nh3(float nh3_ppm);
void hmi_send_co2(uint16_t co2_ppm);
void hmi_send_history_record(uint8_t index, const char *record);

/* ─── 手动/自动模式 ─── */
void hmi_enter_manual_mode(void);
void hmi_exit_manual_mode(void);
bool hmi_is_manual_mode(void);

#endif /* MOD_HMI_H_ */
