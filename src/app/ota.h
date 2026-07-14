/**
 * @file    ota.h
 * @brief   OTA 固件升级模块接口
 * @note    ESP32 通过 SPI 发送 OTA 帧到 STM32，本模块处理并写入 Flash
 *
 * OTA 帧格式 (ESP32 → STM32):
 *   握手帧: [0xAA][0x55][0xF0][file_size:4BE][version:4BE][sig:32][crc8] = 44B
 *   数据帧: [0xAA][0x55][0xF1][offset:4BE][len:2BE][data:N][crc8] = 10+N B
 *   结果帧: [0xAA][0x55][0xF2][status:1][crc8] = 6B
 */

#ifndef APP_OTA_H_
#define APP_OTA_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== OTA 状态 ========================== */
typedef enum {
    OTA_IDLE        = 0,
    OTA_CHECKING    = 1,
    OTA_DOWNLOADING = 2,
    OTA_SUCCESS     = 3,
    OTA_FAILED      = 4
} OtaStatus_t;

/** OTA 错误码 */
typedef enum {
    OTA_ERR_NONE        = 0,
    OTA_ERR_NETWORK     = 1,
    OTA_ERR_SIGNATURE   = 2,
    OTA_ERR_FLASH       = 3,
    OTA_ERR_TIMEOUT     = 4,
    OTA_ERR_NO_UPDATE   = 5
} OtaError_t;

/* ========================== 帧偏移常量 ========================== */
#define OTA_HDR_SIZE   3      /* [0xAA][0x55][type] */

/* 握手帧 (0xF0, 44 bytes) */
#define OTA_HANDSHAKE_LEN     44
#define OTA_HS_OFF_TYPE       2
#define OTA_HS_OFF_FILE_SIZE  3    /* uint32 BE */
#define OTA_HS_OFF_VERSION    7    /* uint32 BE */
#define OTA_HS_OFF_SIGNATURE  11   /* 32 bytes */
#define OTA_HS_CRC_OFF        43

/* 数据帧 (0xF1, 10+len bytes) */
#define OTA_DATA_HDR_LEN      10   /* 2hdr + 1type + 4offset + 2len + 1crc */
#define OTA_DATA_OFF_OFFSET   3    /* uint32 BE */
#define OTA_DATA_OFF_LEN      7    /* uint16 BE */
#define OTA_DATA_OFF_PAYLOAD  9
#define OTA_MAX_CHUNK         512  /* 每块最大 512 字节 */

/* 结果帧 (0xF2, 6 bytes) */
#define OTA_RESULT_LEN        6
#define OTA_RES_OFF_STATUS    3

/* ========================== API ========================== */

/**
 * @brief  启动成功后确认（写 boot_success=0xAA 到元数据区）
 * @note   必须在 setup() 末尾、IWDG 超时前调用
 */
void ota_confirm_success(void);

/**
 * @brief  注册 ESP32 OTA 帧回调（在 esp32_init 后调用）
 * @note   注册后 ESP32 模块收到 OTA 帧时将自动回调本模块
 */
void ota_register_spi_callback(void);

/**
 * @brief  SPI OTA 帧回调处理（由 ESP32 模块调用）
 * @param  type  帧类型: 0xF0/0xF1/0xF2
 * @param  data  完整帧数据 (含 header)
 * @param  len   帧总长度
 */
void ota_on_spi_frame(uint8_t type, const uint8_t *data, uint16_t len);

/**
 * @brief  上电自动检查更新（通过 ESP32 SPI 查询服务器最新版本）
 * @note   在 setup() 初始化完成后、ota_confirm_success() 之前调用
 *         若检测到回滚启动（try_count > 0），自动跳过
 */
void ota_check_version(void);

/**
 * @brief  手动触发 OTA 更新（Modbus 寄存器触发）
 * @note   通过 SPI MISO 发送 ESP32_CMD_OTA_START 命令给 ESP32
 */
void ota_trigger(void);

/**
 * @brief  同步 OTA 状态到 Modbus 寄存器（主循环定期调用）
 */
void ota_update_modbus_regs(void);

/**
 * @brief  获取 OTA 状态
 */
OtaStatus_t ota_get_status(void);

/**
 * @brief  获取 OTA 下载进度 (0-100)
 */
uint8_t ota_get_progress(void);

/**
 * @brief  获取 OTA 错误码
 */
uint8_t ota_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OTA_H_ */
