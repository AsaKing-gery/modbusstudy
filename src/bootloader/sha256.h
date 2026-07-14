/**
 * @file    sha256.h
 * @brief   SHA-256 哈希算法（纯 C，零依赖，~2.5KB ROM）
 */

#ifndef SHA256_H_
#define SHA256_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t  data[SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[SHA256_DIGEST_SIZE]);

/** 一步计算 SHA256 */
void sha256(const uint8_t *data, size_t len, uint8_t hash[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H_ */
