/**
 * @file    boot_config.h
 * @brief   OTA Bootloader 共享配置（Bootloader 和 APP 共用）
 */

#ifndef BOOT_CONFIG_H_
#define BOOT_CONFIG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== Flash 分区地址 ========================== */
#define APP1_START      0x08020000
#define APP2_START      0x08040000
#define APP_SIZE        0x00020000      /* 128KB */
#define METADATA_ADDR   0x0800C000
#define METADATA_SECTOR 3               /* STM32F4 扇区 3 = 16KB */

/* ========================== 安全常量 ========================== */
#define SIGNATURE_SIZE  32              /* HMAC-SHA256 */
#define HMAC_KEY_SIZE   32
#define BOOT_SUCCESS    0xAA
#define MAX_RETRIES     3
#define MAGIC_VALID     0xA5A5A5A5

/* ========================== HMAC 密钥（与服务器 sign.py 一致） ========================== */
#define HMAC_KEY \
    0x4B,0x73,0x8A,0x1F,0x2E,0x6D,0x9C,0xB0, \
    0x3F,0x58,0x7D,0x12,0x8E,0xA4,0x61,0xF5, \
    0x0C,0x39,0xD7,0xAB,0x45,0x6E,0x82,0x19, \
    0x94,0xBF,0xE3,0x27,0x50,0x68,0xDC,0xFF

/* ========================== IWDG 超时 ========================== */
#define BOOT_WATCHDOG_TIMEOUT_S  30     /* APP 必须在 30s 内确认启动成功 */

/* ========================== 元数据结构 ========================== */
typedef struct {
    uint8_t  signature[SIGNATURE_SIZE]; /* HMAC-SHA256 */
    uint32_t version;
    uint8_t  try_count;
    uint8_t  max_tries;
    uint8_t  boot_success;
    uint8_t  reserved[2];              /* 4 字节对齐 */
} AppSlotMeta_t;  /* 44 bytes */

typedef struct {
    uint8_t       active_slot;         /* 0=APP1, 1=APP2 */
    uint8_t       reserved[3];
    AppSlotMeta_t slot[2];             /* [0]=APP1, [1]=APP2 */
    uint32_t      magic;               /* MAGIC_VALID = 有效 */
} BootMetadata_t;  /* 4 + 4 + 44*2 + 4 = 96 bytes */

#ifdef __cplusplus
}
#endif

#endif /* BOOT_CONFIG_H_ */
