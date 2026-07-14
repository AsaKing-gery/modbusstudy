/**
 * @file    hmi.cpp
 * @brief   淘晶驰VT串口屏通信实现
 * @note    SoftwareSerial on PA15(TX) / PC10(RX), baud 19200
 *          自定义帧协议: [head][data...] (head 仅帧首)
 *          可变长帧 (0x02/0x0E/0x0F): 读到尾字节(==head)为止
 */

#include "hmi.h"
#include "modbus/modbus_core.h"
#include "bsp/bsp_debug.h"
#include <SoftwareSerial.h>
#include <stdarg.h>
#include <FreeRTOS.h>
#include <task.h>
#include <IWatchdog.h>

/* ========================== 串口屏实例 ========================== */
static SoftwareSerial HMISerial(PIN_HMI_RX, PIN_HMI_TX);

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

/* ========================== 延迟保存 (Main 任务统一写 EEPROM) ========================== */
/* 
 * 注意: 自动 EEPROM 保存已禁用. STM32F4 Flash 擦写期间 CPU 被 HAL 轮询占用,
 * 即使预先 IWatchdog.reload() 也无法在擦写循环内喂狗, 必然触发 WDT 复位.
 * 阈值设置立即写入 g_regs(RAM), 断电前通过 Modbus CMD_SAVE(0x0A) 手动持久化.
 */
static volatile bool g_hmi_save_pending __attribute__((unused)) = false;
void hmi_mark_save_pending(void) { /* 预留, 当前不自动保存 */ }
bool hmi_is_save_pending(void) { return false; }

/* ========================== 命令处理 ========================== */

static volatile bool g_manual_override = false;

void hmi_enter_manual_mode(void) { g_manual_override = true; }
void hmi_exit_manual_mode(void) { g_manual_override = false; }
bool hmi_is_manual_mode(void)    { return g_manual_override; }

static void handle_device_control(uint8_t head, uint8_t value)
{
    uint8_t bit_idx = ((head >> 4) & 0x0F) - 1;
    if (bit_idx > 7) return;

    uint16_t state = modbus_reg_get(REG_OUTPUT_STATE);
    if (value != 0) {
        state |= (1 << bit_idx);
        g_manual_override = true;   /* 任意ON → 手动模式 */
    } else {
        state &= ~(1 << bit_idx);
        if ((state & 0xFF) == 0) {
            g_manual_override = false;  /* 全部OFF → 退回自动 */
        }
    }
    modbus_reg_set(REG_OUTPUT_STATE, state);
    if (g_modbus_output_cb) {
        g_modbus_output_cb(state & 0xFF);
    }

    DBG("HMI", "dev 0x" + String(head, HEX) + " ch" + String(bit_idx)
        + "=" + (value ? "ON" : "OFF") + " manual=" + String(g_manual_override));
}

static void handle_threshold(uint8_t head, uint8_t value)
{
    uint8_t idx = head - HMI_HEAD_THRESHOLD_A;
    modbus_reg_set(REG_THRESHOLD_A + idx, (uint16_t)value);
    DBG("HMI", "thresh " + String(idx) + "=" + String(value));
}

/* ========================== 页面3 两段拼接浮点解析 ========================== */

/**
 * @brief 解析两个无分隔符拼接的浮点字符串 (如 "30.020.0" → {30.0, 20.0})
 * @return 成功解析的数量 (0/1/2)
 */
static int parse_two_floats(const char *data, uint8_t len, float *v1, float *v2)
{
    for (uint8_t split = 1; split < len; split++) {
        char buf1[16], buf2[16];
        if (split >= (int)sizeof(buf1) || (len - split) >= (int)sizeof(buf2)) continue;

        memcpy(buf1, data, split);
        buf1[split] = '\0';
        memcpy(buf2, data + split, len - split);
        buf2[len - split] = '\0';

        char *end1, *end2;
        float f1 = strtof(buf1, &end1);
        float f2 = strtof(buf2, &end2);

        if (end1 == buf1 + split && end2 == buf2 + (len - split)) {
            *v1 = f1;
            *v2 = f2;
            return 2;
        }
    }
    return 0;
}

/**
 * @brief 处理页面3传来的阈值帧
 * @note  0x0E 新格式: "下限;上限" (有;分隔符, 见 STM32对接文档.md §4.2)
 *        0x0F/0x02 仍为两段无分隔符拼接 (MD §9 标注"本次未改")
 */
static void handle_page3_string(uint8_t head, const char *data, uint8_t len)
{
    switch (head) {
    case HMI_HEAD_PARAM_E: { /* 0x0E: 温度 下限;上限 (二进制格式: lo_byte, sep, hi_byte) */
        /* 格式: [0E][lo][00][hi][0E], lo/hi 为 uint8 */
        if (len < 3) {
            DBG("HMI", "temp parse fail, too short");
            return;
        }
        int lo = (uint8_t)data[0];
        int hi = (uint8_t)data[2];
        if (lo >= 0 && hi > lo && hi <= 100) {
            modbus_reg_set(REG_TEMP_LO_X100, (uint16_t)(lo * 100));
            modbus_reg_set(REG_TEMP_HI_X100, (uint16_t)(hi * 100));
            g_hmi_save_pending = true;
            DBG("HMI", "temp limits " + String(lo) + "/" + String(hi));
        }
        break;
    }
    case HMI_HEAD_PARAM_F: { /* 0x0F: 湿度 二进制: [0F][lo][hi][0F] */
        if (len < 2) { DBG("HMI", "humi parse fail, too short"); return; }
        int lo = (uint8_t)data[0];
        int hi = (uint8_t)data[1];
        modbus_reg_set(REG_HUMI_HI_X100, (uint16_t)(hi * 100));
        modbus_reg_set(REG_HUMI_LO_X100, (uint16_t)(lo * 100));
        g_hmi_save_pending = true;
        DBG("HMI", "humi limits " + String(lo) + "/" + String(hi));
        break;
    }
    case HMI_HEAD_PARAM_GROUP: { /* 0x02: NH3 二进制: [02][lo][hi][02] */
        if (len < 2) { DBG("HMI", "nh3 parse fail, too short"); return; }
        int lo = (uint8_t)data[0];
        int hi = (uint8_t)data[1];
        modbus_reg_set(REG_NH3_HI_X100, (uint16_t)(hi * 100));
        modbus_reg_set(REG_NH3_LO_X100, (uint16_t)(lo * 100));
        g_hmi_save_pending = true;
        DBG("HMI", "nh3 limits " + String(lo) + "/" + String(hi));
        break;
    }
    }
}

/**
 * @brief 处理页面3控件28的 CO2 阈值 (二进制 uint16 大端)
 *        帧格式: [0x01][hi_H][hi_L][lo_H][lo_L][0x01] (固定6字节)
 */
static void handle_page3_co2(const uint8_t *buf)
{
    uint16_t co2_hi = ((uint16_t)buf[1] << 8) | buf[2];
    uint16_t co2_lo = ((uint16_t)buf[3] << 8) | buf[4];
    modbus_reg_set(REG_CO2_HI, co2_hi);
    modbus_reg_set(REG_CO2_LO, co2_lo);
    g_hmi_save_pending = true;
    DBG("HMI", "co2 limits " + String(co2_hi) + "/" + String(co2_lo));
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

/* ========================== 传感器数据发送 ==========================
 * 复合帧方案: 4个传感器值拼成1帧, 一次 write() 发出
 * 帧格式: [0xA5][temp][,][humi][,][nh3][,][co2]
 * 例: A5 33 30 2E 30 2C 33 30 2E 30 2C 33 30 2E 30 2C 33 30  = "30.0,30.0,30.0,30"
 *
 * 为什么用复合帧:
 *   SoftwareSerial 在 FreeRTOS 下 bit-bang 时序无法保证,
 *   帧间 or 帧内可能出现 >10ms 字节间隙 → 屏按超时分帧 → 值被截断跳动.
 *   但同一次 write(buf,len) 内部, 所有字节由 ISR 连续处理,
 *   字节间仅间隔一个 ISR 重入周期 (~几 us), 远小于 10ms, 不会被截断.
 */

/**
 * @brief 构建并发送单帧 (单传感器, 兼容旧接口, 也供历史记录等使用)
 */
static void hmi_write_frame(uint8_t head, const char *val)
{
    uint8_t buf[16];
    uint8_t len = 1;
    buf[0] = head;
    while (*val && len < sizeof(buf) - 1) {  /* 留 1 字节给帧尾 */
        buf[len++] = (uint8_t)(*val++);
    }
    buf[len++] = head;  /* 帧尾 = 帧头, 屏端用帧尾识别完整帧 */

    /* 禁任务切换, 保证 5 字节帧不被其他任务打断.
     * ISR 仍可打断但微秒级, 不会触发 30ms 分帧. */
    vTaskSuspendAll();
    HMISerial.write(buf, len);
    xTaskResumeAll();
    HMISerial.flush();
    /* 屏端用帧头尾匹配提取, 不再依赖超时分帧, 帧间距只需保证字节不粘连即可 */
    vTaskDelay(pdMS_TO_TICKS(10));
}

/**
 * @brief 复合帧发送: 温度/湿度/NH3/CO2 合并为一帧
 * @param fmt  snprintf 格式串, 如 "%.1f,%.1f,%.1f,%u"
 * @note   临时提权到最高优先级发送, 防止任务切换打断 write().
 *         不用 vTaskSuspendAll, 因为 flush() 内部可能调 vTaskDelay(挂起时无效).
 *         屏端超时 30ms 作为兜底保护.
 */
static void hmi_write_compound_frame(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* buf[0] 留给帧头, 数据从 buf[1] 开始 */
    uint8_t buf[48];
    int n = vsnprintf((char *)(buf + 1), sizeof(buf) - 2, fmt, args);
    va_end(args);

    if (n > 0 && n < (int)(sizeof(buf) - 2)) {
        buf[0] = HMI_HEAD_COMPOUND;

        /* ─── 调试: 打印帧内容 ─── */
        DEBUG_SERIAL.print("[HMI.TX] cmp len=");
        DEBUG_SERIAL.print(n + 1);
        DEBUG_SERIAL.print(" str='");
        DEBUG_SERIAL.print((const char *)(buf + 1));
        DEBUG_SERIAL.print("' hex=");
        for (int i = 0; i < n + 1; i++) {
            if (buf[i] < 0x10) DEBUG_SERIAL.print('0');
            DEBUG_SERIAL.print(buf[i], HEX);
            DEBUG_SERIAL.print(' ');
        }
        DEBUG_SERIAL.println();
        DEBUG_SERIAL.flush();

        /* 禁任务切换 + 手动喂狗: 保证 write() 不被其他任务抢占,
         * 但 SysTick/ISR 照跑, WDT 不受影响.
         * 18bytes × 10bits / 19200baud ≈ 9.4ms, 远小于 WDT 400ms. */
        IWatchdog.reload();
        DEBUG_SERIAL.print("[HMI.TX] lock: ");
        uint32_t t0 = micros();
        vTaskSuspendAll();
        HMISerial.write(buf, (size_t)(n + 1));
        xTaskResumeAll();
        HMISerial.flush();
        uint32_t t1 = micros();

        DEBUG_SERIAL.print(t1 - t0);
        DEBUG_SERIAL.println(" us");
        DEBUG_SERIAL.flush();

        vTaskDelay(pdMS_TO_TICKS(HMI_FRAME_GAP_MS));
    }
}

void hmi_send_temperature(float temp_c)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", temp_c);
    hmi_write_frame(HMI_HEAD_TEMP, buf);
}

void hmi_send_humidity(float humi_pct)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", humi_pct);
    hmi_write_frame(HMI_HEAD_HUMID, buf);
}

void hmi_send_nh3(float nh3_ppm)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", nh3_ppm);
    hmi_write_frame(HMI_HEAD_NH3, buf);
}

void hmi_send_co2(uint16_t co2_ppm)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", co2_ppm);
    hmi_write_frame(HMI_HEAD_CO2, buf);
}

void hmi_send_history_record(uint8_t index, const char *record)
{
    uint8_t head;
    switch (index) {
        case 2:  head = HMI_HEAD_HISTORY2; break;
        case 3:  head = HMI_HEAD_HISTORY3; break;
        default: head = HMI_HEAD_HISTORY1; break;
    }
    hmi_write_frame(head, record);
}

/* ========================== 初始化 ========================== */

void hmi_init(void)
{
    TRACE("S");
    HMISerial.begin(HMI_BAUDRATE);
    TRACE(".");
    TRACE("s");
}

/* ========================== 接收处理 (静态, 避免死代码重复) ========================== */

/**
 * @brief 处理 HMISerial 接收缓冲区中的所有字节
 * @return true=至少处理了 1 个字节
 */
static bool hmi_process_rx(uint8_t *buf, uint8_t &idx)
{
    bool processed = false;
    while (HMISerial.available() > 0) {
        processed = true;
        uint8_t b = HMISerial.read();

        /* 打印每个接收字节 */
        if (b < 0x10) DEBUG_SERIAL.print('0');
        DEBUG_SERIAL.print(b, HEX);
        DEBUG_SERIAL.print(' ');

        if (idx == 0) {
            /* 等待有效帧头 (VT 0xEE 等丢弃) */
            if (is_valid_head(b)) {
                buf[0] = b;
                idx = 1;
            }
        } else {
            if (idx >= 32) { idx = 0; continue; } /* buf[32], 溢出丢弃 */
            buf[idx++] = b;

            uint8_t head = buf[0];

            /* ─── 0x01: CO2 阈值 (固定 6 字节, 二进制) ─── */
            if (head == HMI_HEAD_NUMERIC) {
                if (idx >= 6) {
                    if (buf[5] == head) {
                        handle_page3_co2(buf);
                    }
                    idx = 0;
                }
            }
            /* ─── 0x02/0x0E/0x0F: 可变长帧 (读到 tail==head) ─── */
            else if (head == HMI_HEAD_PARAM_GROUP
                  || head == HMI_HEAD_PARAM_E
                  || head == HMI_HEAD_PARAM_F) {
                if (b == head) {
                    /* 帧完整: 需至少 head+2data+tail=4字节 */
                    if (idx >= 4) {
                        handle_page3_string(head, (const char *)&buf[1], idx - 2);
                    }
                    idx = 0;
                }
                /* 否则继续接收直到遇到 tail */
            }
            /* ─── 设备控制: 0x10~0x80 (固定 3 字节) ─── */
            else if (head >= HMI_HEAD_DEV_BASE && head <= HMI_HEAD_DEV_END) {
                if (idx >= 3) {
                    if (buf[2] == head) {
                        handle_device_control(head, buf[1]);
                    }
                    idx = 0;
                }
            }
            /* ─── 阈值命令: 0x0A~0x0D (固定 3 字节) ─── */
            else if (head >= HMI_HEAD_THRESHOLD_A && head <= HMI_HEAD_THRESHOLD_D) {
                if (idx >= 3) {
                    if (buf[2] == head) {
                        handle_threshold(head, buf[1]);
                    }
                    idx = 0;
                }
            }
            /* ─── 末级兜底: idx超限 or 未知帧头则丢弃 ─── */
            else if (idx >= 6) {
                idx = 0;
            }
        }
    }
    return processed;
}

/* ========================== 接收任务 ========================== */

void hmi_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(800));
    DBG("HMI", "started");

    uint8_t buf[32];          /* 32 字节缓冲, 容纳可变长帧 */
    uint8_t idx = 0;
    uint32_t last_send_ms = 0;
    uint32_t send_cnt = 0;      /* 调试: 发送计数 */

    while (true) {
        /* ─── 每个主循环先处理接收 ─── */
        hmi_process_rx(buf, idx);

        /* ================================================================
         * 传感器数据发送 (5s 周期)
         * ================================================================ */
        if (millis() - last_send_ms >= 5000) {
            last_send_ms = millis();
            send_cnt++;

#if HMI_SIM_ENABLE
            modbus_reg_set(REG_TEMP_X100, 1000);
            modbus_reg_set(REG_HUMI_X100, 1000);
            modbus_reg_set(REG_CO2,       10);
            modbus_reg_set(REG_NH3_X100,  1000);
            modbus_reg_set(REG_ESP32_STATUS, 0x02);

            /* ─── 阈值: 10.0 超过上限(风机ON) 且 低于下限(加湿器ON) ─── */
            modbus_reg_set(REG_TEMP_HI_X100,  500);   /*  5.0°C → 10.0 > 5.0, 风机ON */
            modbus_reg_set(REG_TEMP_LO_X100, 1500);   /* 15.0°C → 10.0 < 15.0, 加湿器ON */
            modbus_reg_set(REG_HUMI_HI_X100,  500);   /*  5.0%  → 风机ON */
            modbus_reg_set(REG_HUMI_LO_X100, 1500);   /* 15.0%  → 加湿器ON */
            modbus_reg_set(REG_CO2_HI,         5);    /*  5 ppm  → 风机ON */
            modbus_reg_set(REG_CO2_LO,        15);    /* 15 ppm  → 不变(10 < 15) */
            modbus_reg_set(REG_NH3_HI_X100,   500);   /*  5.0ppm → 风机ON */
            modbus_reg_set(REG_NH3_LO_X100,  1500);   /* 15.0ppm → 不变 */

            DEBUG_SERIAL.print("[HMI.TX] #"); DEBUG_SERIAL.print(send_cnt);
            DEBUG_SERIAL.print(" t="); DEBUG_SERIAL.println(millis());

            /* 逐个发 0xA1~0xA4, 每帧 5 字节, 帧间距 50ms */
            hmi_write_frame(HMI_HEAD_TEMP,  "10.0"); hmi_process_rx(buf, idx);
            hmi_write_frame(HMI_HEAD_HUMID, "10.0"); hmi_process_rx(buf, idx);
            hmi_write_frame(HMI_HEAD_NH3,   "10.0"); hmi_process_rx(buf, idx);
            hmi_write_frame(HMI_HEAD_CO2,   "10.0"); hmi_process_rx(buf, idx);

#else
            uint16_t status = modbus_reg_get(REG_ESP32_STATUS);
            if (status & 0x02) {
                int16_t temp_raw = (int16_t)modbus_reg_get(REG_TEMP_X100);
                int16_t humi_raw = (int16_t)modbus_reg_get(REG_HUMI_X100);
                int16_t nh3_raw  = (int16_t)modbus_reg_get(REG_NH3_X100);
                uint16_t co2_raw = modbus_reg_get(REG_CO2);

                char sbuf[8];
                snprintf(sbuf, sizeof(sbuf), "%.1f", temp_raw / 100.0f);
                hmi_write_frame(HMI_HEAD_TEMP, sbuf); hmi_process_rx(buf, idx);
                snprintf(sbuf, sizeof(sbuf), "%.1f", humi_raw / 100.0f);
                hmi_write_frame(HMI_HEAD_HUMID, sbuf); hmi_process_rx(buf, idx);
                snprintf(sbuf, sizeof(sbuf), "%.1f", nh3_raw / 100.0f);
                hmi_write_frame(HMI_HEAD_NH3, sbuf); hmi_process_rx(buf, idx);
                snprintf(sbuf, sizeof(sbuf), "%u", co2_raw);
                hmi_write_frame(HMI_HEAD_CO2, sbuf); hmi_process_rx(buf, idx);
            }
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
