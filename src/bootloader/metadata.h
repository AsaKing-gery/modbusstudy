/**
 * @file    metadata.h
 * @brief   元数据区管理接口
 */

#ifndef METADATA_H_
#define METADATA_H_

#include "boot_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 从 Flash 读取元数据 */
void metadata_read(BootMetadata_t *meta);

/** 写入元数据到 Flash（先擦除扇区再写入） */
void metadata_write(const BootMetadata_t *meta);

/** 初始化元数据为出厂默认值 */
void metadata_init_default(BootMetadata_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* METADATA_H_ */
