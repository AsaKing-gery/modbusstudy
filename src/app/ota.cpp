/**
 * @file    ota.cpp
 * @brief   OTA 固件升级模块 — SPI 下载实现 (适配工业级握手协议)
 * @note    ESP32 通过 SPI 发送 OTA 帧到 STM32:
 *          1. ESP32 发起握手 → 获取 STM32 版本 → 对比服务器版本
 *          2. 若需更新: 握手帧 0xF0 → 擦除分区 → 数据帧 0xF1 → 写入Flash → 结果帧 0xF2 → 复位
 *          3. 若无需更新: 发送 HANDSHAKE_ACK → 进入正常数据交换
 */

#include "ota.h"
#include "boot_config.h"
#include "bsp/bsp_debug.h"
#include "modbus/modbus_core.h"
#include "modbus/modbus_rtu.h"
#include "modules/esp32.h"
#include <stm32f4xx_hal.h>
#include <IWatchdog.h>
#include <string.h>

/* ========================== 运行状态 ========================== */
static OtaStatus_t ota_status     = OTA_IDLE;
static uint8_t     ota_progress   = 0;
static OtaError_t  ota_error      = OTA_ERR_NONE;
static bool        ota_confirmed  = false;
static bool        has_checked    = false;  /**< 本次上电已检查过，防止重复触发 */

/* OTA 检查超时：检查/下载有 60 秒完成窗口 */
static uint32_t    ota_check_start_ms = 0;
#define OTA_CHECK_TIMEOUT_MS   60000

/* 当前 OTA 下载上下文 */
static uint32_t    ota_new_version   = 0;
static uint32_t    ota_file_size     = 0;
static uint8_t     ota_new_slot      = 0;
static uint32_t    ota_target_addr   = 0;
static uint32_t    ota_bytes_written = 0;

/* metadata 延迟写入: 握手时暂存, 数据全部收完才写 flash */
static uint8_t     g_ota_pending_sig[SIGNATURE_SIZE];
static uint32_t    g_ota_pending_version;

/* ========================== Flash 操作（HAL 封装） ========================== */

static void ota_flash_unlock(void)
{
    HAL_FLASH_Unlock();
}

static void ota_flash_lock(void)
{
    HAL_FLASH_Lock();
}

static void ota_flash_erase_sector(uint8_t sector)
{
    uint32_t error;
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = sector;
    erase.NbSectors    = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    /* Flash 擦除期间 CPU 暂停, 提前喂狗 (2000ms 超时 vs ~800ms 擦除) */
    IWatchdog.reload();
    ota_flash_unlock();
    __disable_irq();
    HAL_FLASHEx_Erase(&erase, &error);
    __enable_irq();
    ota_flash_lock();
}

static void ota_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0) return;

    IWatchdog.reload();  /* 每次写 Flash 前喂狗 (~6ms 写, WDT 无法在此间运行) */
    ota_flash_unlock();

    uint32_t offset = 0;
    /* 非对齐前导处理 */
    if (addr & 0x3) {
        uint32_t aligned = addr & ~0x3;
        uint32_t cur = *(volatile uint32_t*)aligned;
        uint32_t shift = (addr & 0x3) * 8;
        uint32_t mask = ~(0xFFUL << shift);
        cur = (cur & mask) | ((uint32_t)data[0] << shift);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, aligned, cur);
        offset++; addr++;
    }

    /* 4 字节对齐写入 */
    while (offset + 4 <= len) {
        uint32_t word = *(uint32_t*)(data + offset);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
        offset += 4; addr += 4;
    }

    /* 尾部处理 */
    if (offset < len) {
        uint32_t cur = *(volatile uint32_t*)addr;
        while (offset < len) {
            uint8_t shift = (addr & 0x3) * 8;
            cur = (cur & ~(0xFFUL << shift)) | ((uint32_t)data[offset] << shift);
            offset++; addr++;
            if ((addr & 0x3) == 0 || offset >= len) {
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr & ~0x3, cur);
                if (addr & 0x3) cur = *(volatile uint32_t*)(addr & ~0x3);
            }
        }
    }

    ota_flash_lock();
}

static void ota_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    const uint8_t *src = (const uint8_t*)addr;
    for (uint32_t i = 0; i < len; i++) buf[i] = src[i];
}

/* ========================== BE 字节序解析 ========================== */

static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/* ========================== 元数据操作 ========================== */

/** 写入 boot_success 确认标志 */
void ota_confirm_success(void)
{
    BootMetadata_t meta;
    ota_flash_read(METADATA_ADDR, (uint8_t*)&meta, sizeof(meta));

    if (meta.magic != MAGIC_VALID) return;

    meta.slot[meta.active_slot].boot_success = BOOT_SUCCESS;
    meta.slot[meta.active_slot].try_count    = 0;

    ota_flash_erase_sector(METADATA_SECTOR);
    ota_flash_write(METADATA_ADDR, (const uint8_t*)&meta, sizeof(meta));

    ota_confirmed = true;
    DBG("OTA", "Boot confirmed");
}

/* ========================== SPI OTA 帧处理 ========================== */

/** 处理 OTA 握手帧 (0xF0) — ESP32 发送固件文件信息 */
static void ota_handle_handshake(const uint8_t *data, uint16_t len)
{
    if (len < OTA_HANDSHAKE_LEN) {
        ota_error = OTA_ERR_SIGNATURE;
        ota_status = OTA_FAILED;
        DBG("OTA", "Handshake too short");
        return;
    }

    /* 只有在 HS_READY 状态才处理 OTA（确保握手已完成） */
    if (!esp32_hs_ready()) {
        DBG("OTA", "Ignoring OTA handshake: SPI handshake not ready");
        return;
    }

    /* 如果正在下载中，忽略重复握手 */
    if (ota_status == OTA_DOWNLOADING) {
        DBG("OTA", "Already downloading, ignoring duplicate handshake");
        return;
    }

    uint32_t file_size = be32(data + OTA_HS_OFF_FILE_SIZE);
    uint32_t version   = be32(data + OTA_HS_OFF_VERSION);
    const uint8_t *sig = data + OTA_HS_OFF_SIGNATURE;

    if (file_size > APP_SIZE) {
        ota_error = OTA_ERR_FLASH;
        ota_status = OTA_FAILED;
        DBG_FMT("OTA", "Firmware too large: %lu > %lu", file_size, (unsigned long)APP_SIZE);
        return;
    }

    /* OTA 始终写到 slot 1 (暂存区), server 固件按 APP1 地址编译 */
    /* Bootloader 校验通过后搬移到 slot 0, 掉电自恢复 */
    ota_new_slot    = 1;
    ota_target_addr = APP2_START;
    ota_new_version = version;
    ota_file_size   = file_size;
    ota_bytes_written = 0;

    /* 擦除目标分区 (喂狗防复位) */
    IWatchdog.reload();
    {
        uint8_t target_sector = (ota_new_slot == 0) ? 5 : 6;
        ota_flash_erase_sector(target_sector);
    }

    /* 暂存签名和版本到 RAM (metadata 延迟到数据全部收完才写 flash) */
    memcpy(g_ota_pending_sig, sig, SIGNATURE_SIZE);
    g_ota_pending_version = version;

    ota_status   = OTA_DOWNLOADING;
    ota_progress = 0;
    ota_error    = OTA_ERR_NONE;

    /* 擦除完成 → 设 ACK 通知 ESP32 开始发数据 */
    esp32_set_miso_cmd(ESP32_CMD_OTA_CHUNK_ACK);

    DBG_FMT("OTA", "Handshake OK: slot=%d ver=%lu size=%lu (metadata deferred)",
            ota_new_slot, (unsigned long)version, (unsigned long)file_size);
}

/** 处理 OTA 数据帧 (0xF1) */
static void ota_handle_data(const uint8_t *data, uint16_t len)
{
    if (ota_status != OTA_DOWNLOADING) return;

    if (len < OTA_DATA_HDR_LEN) {
        ota_error = OTA_ERR_FLASH;
        ota_status = OTA_FAILED;
        DBG("OTA", "Data frame too short");
        return;
    }

    uint32_t offset    = be32(data + OTA_DATA_OFF_OFFSET);
    uint16_t chunk_len = be16(data + OTA_DATA_OFF_LEN);

    if (chunk_len > OTA_MAX_CHUNK) {
        ota_error = OTA_ERR_FLASH;
        ota_status = OTA_FAILED;
        return;
    }

    if (offset + chunk_len > ota_file_size) {
        ota_error = OTA_ERR_FLASH;
        ota_status = OTA_FAILED;
        DBG("OTA", "Chunk exceeds file size");
        return;
    }

    /* 收到新数据块 → 清除上轮 ACK (表示正在处理) */
    esp32_clear_miso_cmd();

    /* 将 payload 拷贝到本地缓冲再写 Flash, 防止 ISR 覆盖 rx_buf 导致数据损坏 */
    {
        static uint8_t local_buf[OTA_MAX_CHUNK];
        memcpy(local_buf, data + OTA_DATA_OFF_PAYLOAD, chunk_len);
        ota_flash_write(ota_target_addr + offset, local_buf, chunk_len);
    }
    ota_bytes_written += chunk_len;

    /* 块写入完成 → MISO 输出 ACK (ESP32 等待此信号后发下一块) */
    esp32_set_miso_cmd(ESP32_CMD_OTA_CHUNK_ACK);

    /* 更新进度 */
    if (ota_file_size > 0) {
        ota_progress = (uint8_t)((ota_bytes_written * 100) / ota_file_size);
    }

    /* 每 10% 打印一次进度 */
    if (ota_progress % 10 == 0 && ota_bytes_written >= chunk_len) {
        DBG_FMT("OTA", "Progress: %d%% (%lu/%lu)",
                ota_progress,
                (unsigned long)ota_bytes_written,
                (unsigned long)ota_file_size);
    }
}

/** 处理 OTA 结果帧 (0xF2) */
static void ota_handle_result(const uint8_t *data, uint16_t len)
{
    uint8_t status = 0xFF;

    if (len < OTA_RESULT_LEN) {
        ota_error = OTA_ERR_SIGNATURE;
        ota_status = OTA_FAILED;
        goto cleanup;
    }

    status = data[OTA_RES_OFF_STATUS];

    if (status == 0x00) {
        if (ota_status == OTA_DOWNLOADING) {
            /* 固件全部收完 → 写入 metadata (通知 bootloader 切换分区) */
            {
                BootMetadata_t meta;
                ota_flash_read(METADATA_ADDR, (uint8_t*)&meta, sizeof(meta));

                memcpy(meta.slot[ota_new_slot].signature, g_ota_pending_sig, SIGNATURE_SIZE);
                meta.slot[ota_new_slot].version       = g_ota_pending_version;
                meta.slot[ota_new_slot].try_count     = 0;
                meta.slot[ota_new_slot].boot_success  = 0x00;
                meta.active_slot                      = ota_new_slot;
                meta.magic                            = MAGIC_VALID;
                /* 保留旧分区签名: bootloader 可回退到旧固件 */

                IWatchdog.reload();
                ota_flash_erase_sector(METADATA_SECTOR);
                ota_flash_write(METADATA_ADDR, (const uint8_t*)&meta, sizeof(meta));
            }

            ota_status = OTA_SUCCESS;
            ota_progress = 100;
            DBG("OTA", "Download complete! Rebooting...");

            for (volatile int i = 0; i < 1000000; i++) { __NOP(); }

            NVIC_SystemReset();
            return;
        } else {
            DBG("OTA", "No update needed, keep running");
            ota_status = OTA_IDLE;
            ota_progress = 0;
            goto cleanup;
        }
    } else {
        /* 下载失败 */
        ota_error = OTA_ERR_SIGNATURE;
        ota_status = OTA_FAILED;
        DBG_FMT("OTA", "ESP32 reported error: 0x%02X", status);
        goto cleanup;
    }

cleanup:
    esp32_clear_miso_cmd();
    ota_progress = 0;
}

/** 握手 ACK 回调: ESP32 已完成握手并决定是否OTA */
static void ota_on_handshake_complete(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;

    /* 握手完成回调: ESP32 已确认握手成功
     * 版本对比由 ESP32 侧完成，STM32 不需要再主动检查
     * OTA_CHECKING 状态不再需要 — 若 ESP32 判断需更新,
     * 会直接发送 OTA_HANDSHAKE 帧触发 ota_handle_handshake() */
    if (ota_status == OTA_CHECKING) {
        /* 手动触发 (ota_trigger) 场景: 握手完成, 等待 ESP32 决定 */
        ota_progress = 0;
        DBG("OTA", "Handshake confirmed, waiting for ESP32 OTA decision...");
    }

    has_checked = true;
}

/** SPI OTA 帧回调入口 */
void ota_on_spi_frame(uint8_t type, const uint8_t *data, uint16_t len)
{
    switch (type) {
    case ESP32_FRAME_OTA_HANDSHAKE:
        ota_handle_handshake(data, len);
        break;
    case ESP32_FRAME_OTA_DATA:
        ota_handle_data(data, len);
        break;
    case ESP32_FRAME_OTA_RESULT:
        ota_handle_result(data, len);
        break;
    case ESP32_FRAME_HANDSHAKE_ACK:
        /* ESP32 握手确认: 版本检查完成，无需更新或准备开始OTA */
        ota_on_handshake_complete(data, len);
        break;
    default:
        break;
    }
}

/* ========================== 回调注册 ========================== */

void ota_register_spi_callback(void)
{
    g_esp32_ota_frame_cb = ota_on_spi_frame;
    DBG("OTA", "SPI callback registered");
}

/* ========================== 状态查询 ========================== */

OtaStatus_t ota_get_status(void)  { return ota_status; }
uint8_t     ota_get_progress(void) { return ota_progress; }
uint8_t     ota_get_error(void)    { return ota_error; }

/* ========================== 手动触发 (Modbus) ========================== */

void ota_trigger(void)
{
    if (ota_status == OTA_DOWNLOADING) {
        DBG("OTA", "Already downloading, ignoring trigger");
        return;
    }

    if (!esp32_hs_ready()) {
        DBG("OTA", "SPI handshake not ready, cannot trigger OTA");
        return;
    }

    DBG("OTA", "Manual OTA trigger via Modbus...");
    ota_status  = OTA_CHECKING;
    ota_error   = OTA_ERR_NONE;
    ota_progress = 0;
    ota_check_start_ms = HAL_GetTick();

    /* 通过 SPI MISO 通知 ESP32 开始 OTA 流程 */
    esp32_set_miso_cmd(ESP32_CMD_OTA_START);
}

/* ========================== 上电自动检查 ========================== */

void ota_check_version(void)
{
    /* 新握手协议: 版本检查由 ESP32 在握手阶段完成
     * STM32 只需等待 ESP32 发起握手 (HS_INIT → HS_RESPONSE → HS_READY)
     * ESP32 在握手响应中获得 STM32 版本号，自行对比服务器版本
     * 若需更新则直接发送 OTA_HANDSHAKE 帧
     *
     * 此处仅做元数据层面的保护:
     *   若检测到回滚启动 (try_count > 0)，跳过自动检查
     */
    if (has_checked) {
        DBG("OTA", "Already checked this boot, skip");
        return;
    }

    DBG("OTA", "Auto-check: waiting for ESP32 handshake...");

    /* 检查元数据区是否有待确认的 OTA（上次 OTA 后未确认成功） */
    BootMetadata_t meta;
    ota_flash_read(METADATA_ADDR, (uint8_t*)&meta, sizeof(meta));

    if (meta.magic == MAGIC_VALID) {
        uint8_t slot = meta.active_slot;
        if (meta.slot[slot].try_count > 0) {
            DBG_FMT("OTA", "Skip auto-check: try_count=%d (possible rollback)",
                    meta.slot[slot].try_count);
            has_checked = true;
            return;
        }
    }

    has_checked = true;
    ota_check_start_ms = HAL_GetTick();
    DBG("OTA", "Auto-check ready, awaiting ESP32 handshake");
}

/* ========================== Modbus 寄存器同步 ========================== */

void ota_update_modbus_regs(void)
{
    /* OTA 检查/下载超时恢复：60 秒无进展 → 回到 IDLE 并清除 MISO 命令 */
    if (ota_status == OTA_CHECKING && ota_check_start_ms != 0) {
        if (HAL_GetTick() - ota_check_start_ms > OTA_CHECK_TIMEOUT_MS) {
            DBG("OTA", "Check timeout, resetting to IDLE");
            ota_status = OTA_IDLE;
            ota_error = OTA_ERR_NONE;
            ota_check_start_ms = 0;
            esp32_clear_miso_cmd();
        }
    }

    modbus_reg_set(REG_OTA_STATUS,   (uint16_t)ota_status);
    modbus_reg_set(REG_OTA_PROGRESS, (uint16_t)ota_progress);
    modbus_reg_set(REG_OTA_ERROR,    (uint16_t)ota_error);
}
