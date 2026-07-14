/**
 * @file    flash_drv.c
 * @brief   STM32F4 内部 Flash 驱动 (HAL 封装)
 * @note    写操作必须 4 字节对齐，擦除操作会短暂屏蔽中断
 */

#include "flash_drv.h"
#include "stm32f4xx_hal.h"

/* ========================== 初始化 ========================== */
void flash_init(void)
{
    /* HAL_FLASH 不需要额外初始化，默认状态即可 */
}

/* ========================== 扇区擦除 ========================== */
void flash_erase_sector(uint8_t sector)
{
    uint32_t sector_error;
    FLASH_EraseInitTypeDef erase_init;

    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector       = sector;
    erase_init.NbSectors    = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;  /* 2.7-3.6V */

    HAL_FLASH_Unlock();
    __disable_irq();
    HAL_FLASHEx_Erase(&erase_init, &sector_error);
    __enable_irq();
    HAL_FLASH_Lock();
}

/* ========================== 单字写入 ========================== */
void flash_write_word(uint32_t addr, uint32_t data)
{
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
    HAL_FLASH_Lock();
}

/* ========================== 批量写入 ========================== */
void flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0) return;

    HAL_FLASH_Unlock();

    /* 处理非 4 字节对齐的前导字节 */
    uint32_t offset = 0;
    if (addr & 0x3) {
        /* 读-修改-写：先读出当前字，合并写入 */
        uint32_t aligned_addr = addr & ~0x3;
        uint32_t current = *(volatile uint32_t*)aligned_addr;
        uint8_t  shift = (addr & 0x3) * 8;
        uint32_t mask = ~((uint32_t)0xFF << shift);
        uint32_t merged = (current & mask) | ((uint32_t)data[offset] << shift);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, aligned_addr, merged);
        offset++;
        addr++;
    }

    /* 按 4 字节写入 */
    while (offset + 4 <= len) {
        uint32_t word = *(uint32_t*)(data + offset);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
        offset += 4;
        addr += 4;
    }

    /* 处理尾部非对齐字节 */
    if (offset < len) {
        uint32_t current = *(volatile uint32_t*)addr;
        while (offset < len) {
            uint8_t shift = (addr & 0x3) * 8;
            uint32_t mask = ~((uint32_t)0xFF << shift);
            current = (current & mask) | ((uint32_t)data[offset] << shift);
            offset++;
            addr++;
            if ((addr & 0x3) == 0 || offset >= len) {
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr & ~0x3, current);
                if (addr & 0x3) {
                    current = *(volatile uint32_t*)(addr & ~0x3);
                }
            }
        }
    }

    HAL_FLASH_Lock();
}

/* ========================== 读取 ========================== */
void flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    const uint8_t *src = (const uint8_t*)addr;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }
}

/* ========================== 解锁/锁定 ========================== */
void flash_unlock(void)
{
    HAL_FLASH_Unlock();
}

void flash_lock(void)
{
    HAL_FLASH_Lock();
}
