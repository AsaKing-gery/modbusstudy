/**
 * @file    hmi.cpp
 * @brief   淘晶驰VT串口屏通信实现
 * @note    SoftwareSerial on PA15(TX) / PC10(RX), baud 19200
 *         自定义帧协议: [head][data...][head] (帧头==帧尾)
 *         VT内部协议 0xEE 帧头自动丢弃
 */

#include "hmi.h"
#include "modbus/modbus_core.h"
#include "drivers/relay.h"
#include "app/app_debug.h"
#include <SoftwareSerial.h>

/* ========================== 串口屏实例 ========================== */
SoftwareSerial HMISerial(PIN_HMI_RX, PIN_HMI_TX);

/* ========================== 帧协议 ========================== */

/** 判断字节是否为有效命令头 */
static bool is_valid_head(uint8_t byte)
{
    if (byte == HMI_HEAD_NUMERIC || byte == HMI_HEAD_PARAM_GROUP) return true;
    if (byte >= HMI_HEAD_THRESHOLD_A && byte <= HMI_HEAD_THRESHOLD_D) return true;
    if (byte == HMI_HEAD_PARAM_E || byte == HMI_HEAD_PARAM_F) return true;
    if (byte >= HMI_HEAD_DEV_BASE && byte <= HMI_HEAD_DEV_END && (byte & 0x0F) == 0) return true;
    return false;
}

/** 获取期望帧长度 */
static uint8_t get_frame_len(uint8_t head)
{
    if (head == HMI_HEAD_NUMERIC)                                   return 6;
    if (head == HMI_HEAD_PARAM_GROUP || head == HMI_HEAD_PARAM_E
        || head == HMI_HEAD_PARAM_F)                                return 4;
    return 3;  /* 阈值 0x0A~0x0D，设备控制 0x10~0x80 */
}

/* ========================== 命令处理 ========================== */

static void handle_device_control(uint8_t head, uint8_t value)
{
    uint8_t bit_idx = ((head >> 4) & 0x0F) - 1;
    if (bit_idx > 7) return;

    uint16_t state = modbus_reg_get(REG_OUTPUT_STATE);
    if (value != 0) {
        state |= (1 << bit_idx);
    } else {
        state &= ~(1 << bit_idx);
    }
    modbus_reg_set(REG_OUTPUT_STATE, state);
    relay_set_all(state & 0xFF);

    DBG("HMI", "dev 0x" + String(head, HEX) + " ch" + String(bit_idx) + "=" + (value ? "ON" : "OFF"));
}

static void handle_threshold(uint8_t head, uint8_t value)
{
    uint8_t idx = head - HMI_HEAD_THRESHOLD_A;
    modbus_reg_set(REG_THRESHOLD_A + idx, (uint16_t)value);
    DBG("HMI", "thresh " + String(idx) + "=" + String(value));
}

/* ========================== 发送 ========================== */

void hmi_send(uint8_t flag, float value, uint8_t decimals)
{
    char buf[16];
    if (decimals == 0) {
        snprintf(buf, sizeof(buf), "%d", (int)value);
    } else {
        snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    }
    HMISerial.write(flag);
    HMISerial.print(buf);
    HMISerial.write(flag);
}

/* ========================== 初始化 ========================== */

void hmi_init(void)
{
    TRACE("S");
    HMISerial.begin(HMI_BAUDRATE);
    TRACE(".");
    TRACE("s");
}

/* ========================== 接收任务 ========================== */

void hmi_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(800));
    DBG("HMI", "started");

    uint8_t buf[8];
    uint8_t idx = 0;

    while (true) {
        while (HMISerial.available() > 0) {
            uint8_t b = HMISerial.read();

            if (idx == 0) {
                if (is_valid_head(b)) {
                    buf[0] = b;
                    idx = 1;
                }
                /* 否则丢弃（VT 0xEE 等） */
            } else {
                buf[idx++] = b;
                uint8_t expected = get_frame_len(buf[0]);

                if (idx >= expected) {
                    /* 帧完整性校验 */
                    if (buf[0] == buf[expected - 1]) {
                        uint8_t head = buf[0];

                        if (head >= HMI_HEAD_DEV_BASE && head <= HMI_HEAD_DEV_END) {
                            handle_device_control(head, buf[1]);
                        } else if (head >= HMI_HEAD_THRESHOLD_A && head <= HMI_HEAD_THRESHOLD_D) {
                            handle_threshold(head, buf[1]);
                        }
                        /* 0x01/0x02/0x0E/0x0F 暂时仅打印 */
                        else if (head == HMI_HEAD_PARAM_E || head == HMI_HEAD_PARAM_F) {
                            DBG("HMI", "param 0x" + String(head, HEX));
                        } else if (head == HMI_HEAD_NUMERIC) {
                            DBG("HMI", "numeric");
                        } else if (head == HMI_HEAD_PARAM_GROUP) {
                            DBG("HMI", "group");
                        }
                    }
#if DEBUG_ENABLE
                    else {
                        DBG("HMI", "frame err head=0x" + String(buf[0], HEX)
                            + " tail=0x" + String(buf[expected - 1], HEX));
                    }
#endif
                    idx = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
