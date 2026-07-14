/**
 * @file    flash_drv.h
 * @brief   STM32F4 内部 Flash 驱动接口
 */

#ifndef FLASH_DRV_H_
#define FLASH_DRV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== API ========================== */

/** 初始化 Flash（无操作，HAL 不要求） */
void flash_init(void);

/** 擦除指定扇区（0-7，对应 16K/64K/128K 扇区） */
void flash_erase_sector(uint8_t sector);

/** 按 32 位对齐写入（4 字节对齐，单次最多写一个字） */
void flash_write_word(uint32_t addr, uint32_t data);

/** 批量写入（自动处理对齐） */
void flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/** 读取 */
void flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/** 解锁 Flash（用于 APP 端写元数据） */
void flash_unlock(void);

/** 锁定 Flash */
void flash_lock(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_DRV_H_ */
