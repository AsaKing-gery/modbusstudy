/**
 * @file    metadata.c
 * @brief   元数据区管理 — 读、写、初始化
 */

#include "metadata.h"
#include "flash_drv.h"
#include <string.h>

void metadata_read(BootMetadata_t *meta)
{
    flash_read(METADATA_ADDR, (uint8_t*)meta, sizeof(BootMetadata_t));
}

void metadata_write(const BootMetadata_t *meta)
{
    flash_erase_sector(METADATA_SECTOR);  /* 整扇区擦除 */
    flash_write(METADATA_ADDR, (const uint8_t*)meta, sizeof(BootMetadata_t));
}

void metadata_init_default(BootMetadata_t *meta)
{
    memset(meta, 0, sizeof(BootMetadata_t));
    meta->active_slot = 0;
    meta->slot[0].max_tries = MAX_RETRIES;
    meta->slot[1].max_tries = MAX_RETRIES;
    meta->magic = MAGIC_VALID;
}
