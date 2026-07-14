/**
 * @file    hmac_sha256.h
 * @brief   HMAC-SHA256 认证码
 */

#ifndef HMAC_SHA256_H_
#define HMAC_SHA256_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMAC_SHA256_DIGEST_SIZE 32

/**
 * @brief  计算 HMAC-SHA256
 * @param  key      密钥
 * @param  key_len  密钥长度（字节）
 * @param  data     数据
 * @param  data_len 数据长度（字节）
 * @param  digest   输出 32 字节 HMAC
 */
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t digest[HMAC_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* HMAC_SHA256_H_ */
