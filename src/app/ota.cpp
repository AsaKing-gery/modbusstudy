/**
 * @file    ota.cpp
 * @brief   OTA 固件升级模块 — SPI 下载实现
 * @note    ESP32 通过 SPI 发送 OTA 帧到 STM32:
 *          握手帧 0xF0 → 擦除分区 → 数据帧 0xF1 → 写入Flash → 结果帧 0xF2 → 元数据/复位
 */

#include "ota.h"
#include "boot_config.h"
#include "bsp/bsp_debug.h"
#include "modbus/modbus_core.h"
#include "modules/esp32.h"
#include <stm32f4xx_hal.h>
#include <string.h>

/* ========================== 运行状态 ========================== */
static OtaStatus_t ota_status     = OTA_IDLE;
static uint8_t     ota_progress   = 0;
static OtaError_t  ota_error      = OTA_ERR_NONE;
static bool        ota_confirmed  = false;

/* OTA 检查超时：检查/下载有 60 秒完成窗口 */
static uint32_t    ota_check_start_ms = 0;
#define OTA_CHECK_TIMEOUT_MS   60000

/* 当前 OTA 下载上下文 */
static uint32_t    ota_new_version  = 0;
static uint32_t    ota_file_size    = 0;
static uint8_t     ota_new_slot     = 0;
static uint32_t    ota_target_addr  = 0;
static uint32_t    ota_bytes_written = 0;

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

    ota_flash_unlock();
    __disable_irq();
    HAL_FLASHEx_Erase(&erase, &error);
    __enable_irq();
    ota_flash_lock();
}

static void ota_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0) return;

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

/** 处理 OTA 握手帧 (0xF0) */
static void ota_handle_handshake(const uint8_t *data, uint16_t len)
{
    if (len < OTA_HANDSHAKE_LEN) {
        ota_error = OTA_ERR_SIGNATURE;
        ota_status = OTA_FAILED;
        DBG("OTA", "Handshake too short");
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

    /* 读取当前元数据 */
    BootMetadata_t meta;
    ota_flash_read(METADATA_ADDR, (uint8_t*)&meta, sizeof(meta));

    /* 确定目标分区 */
    ota_new_slot   = (meta.active_slot == 0) ? 1 : 0;
    ota_target_addr = (ota_new_slot == 0) ? APP1_START : APP2_START;
    ota_new_version = version;
    ota_file_size   = file_size;
    ota_bytes_written = 0;

    /* 擦除目标分区 */
    uint8_t target_sector = (ota_new_slot == 0) ? 5 : 6;
    ota_flash_erase_sector(target_sector);

    /* 更新元数据 */
    memcpy(meta.slot[ota_new_slot].signature, sig, SIGNATURE_SIZE);
    meta.slot[ota_new_slot].version       = version;
    meta.slot[ota_new_slot].try_count     = 0;
    meta.slot[ota_new_slot].boot_success  = 0x00;
    meta.active_slot                      = ota_new_slot;
    meta.magic                            = MAGIC_VALID;

    /* 清除旧分区签名以防回退混乱 */
    meta.slot[ota_new_slot ^ 1].signature[0] = 0;

    ota_flash_erase_sector(METADATA_SECTOR);
    ota_flash_write(METADATA_ADDR, (const uint8_t*)&meta, sizeof(meta));

    ota_status   = OTA_DOWNLOADING;
    ota_progress = 0;
    ota_error    = OTA_ERR_NONE;

    DBG_FMT("OTA", "Handshake OK: slot=%d ver=%lu size=%lu",
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
    const uint8_t *payload = data + OTA_DATA_OFF_PAYLOAD;

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

    /* 写入 Flash */
    ota_flash_write(ota_target_addr + offset, payload, chunk_len);
    ota_bytes_written += chunk_len;

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

    if (ota_status == OTA_CHECKING && status == 0x00) {
        /* 版本检查完成: 服务器版本 <= 当前, 无需更新 */
        DBG("OTA", "Version check: no update needed");
        ota_status = OTA_IDLE;
        ota_check_start_ms = 0;
        goto cleanup;
    }

    if (status == 0x00) {
        /* 下载完成: OTA_DOWNLOADING 状态下收到成功结果 */
        ota_status = OTA_SUCCESS;
        ota_progress = 100;
        DBG("OTA", "Download complete! Rebooting...");

        /* 延时让 ESP32 完成最后一帧 */
        for (volatile int i = 0; i < 1000000; i++) { __NOP(); }

        NVIC_SystemReset();
        return;  /* 不会执行到这里 */
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

/* ========================== 手动触发 ========================== */

void ota_trigger(void)
{
    if (ota_status == OTA_DOWNLOADING) {
        DBG("OTA", "Already downloading, ignoring trigger");
        return;
    }

    DBG("OTA", "Trigger OTA check via ESP32...");
    ota_status  = OTA_CHECKING;
    ota_error   = OTA_ERR_NONE;
    ota_progress = 0;
    ota_check_start_ms = HAL_GetTick();  /* 记录检查开始时间 */

    /* 通过 SPI MISO 通知 ESP32 开始 OTA 流程 */
    esp32_set_miso_cmd(ESP32_CMD_OTA_START);

    /* ESP32 收到命令后会:
     *   1. 连接 WiFi
     *   2. 请求服务器 /latest.txt → 比较版本
     *   3. 如需更新 → 下载 firmware.bin + firmware.sig
     *   4. 通过 SPI 发送 OTA 握手帧 (0xF0) → 数据帧 (0xF1) → 结果帧 (0xF2)
     * STM32 被动接收 OTA 帧，由 ota_on_spi_frame() 处理
     */
}

/* ========================== 上电自动检查 ========================== */

void ota_check_version(void)
{
    DBG("OTA", "Auto-check version on boot...");

    /* 检查元数据区是否有待确认的 OTA（上次 OTA 后未确认成功） */
    BootMetadata_t meta;
    ota_flash_read(METADATA_ADDR, (uint8_t*)&meta, sizeof(meta));

    if (meta.magic == MAGIC_VALID) {
        uint8_t slot = meta.active_slot;
        if (meta.slot[slot].try_count > 0) {
            /* 上次启动校验失败次数 > 0 → 可能是回滚启动，暂不自动检查 */
            DBG_FMT("OTA", "Skip auto-check: try_count=%d (possible rollback)",
                    meta.slot[slot].try_count);
            return;
        }
    }

    /* 通过 SPI 通知 ESP32 检查版本 */
    esp32_set_miso_cmd(ESP32_CMD_OTA_START);
    ota_status = OTA_CHECKING;
    ota_check_start_ms = HAL_GetTick();  /* 记录检查开始时间 */
    DBG("OTA", "Auto-check triggered");
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
