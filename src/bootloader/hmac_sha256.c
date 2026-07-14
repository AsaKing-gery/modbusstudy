/**
 * @file    hmac_sha256.c
 * @brief   HMAC-SHA256 实现 (RFC 2104)
 * @note    ipad/opad 方法，使用 SHA256 底层
 */

#include "hmac_sha256.h"
#include "sha256.h"
#include <string.h>

#define SHA256_BLOCK_SIZE 64

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t digest[HMAC_SHA256_DIGEST_SIZE])
{
    uint8_t  key_block[SHA256_BLOCK_SIZE];
    uint8_t  ipad[SHA256_BLOCK_SIZE];
    uint8_t  opad[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;
    uint8_t   inner_hash[SHA256_DIGEST_SIZE];

    /* 密钥处理：若超过块大小，先哈希 */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, key_block);
        memset(key_block + SHA256_DIGEST_SIZE, 0, SHA256_BLOCK_SIZE - SHA256_DIGEST_SIZE);
    } else {
        memcpy(key_block, key, key_len);
        if (key_len < SHA256_BLOCK_SIZE)
            memset(key_block + key_len, 0, SHA256_BLOCK_SIZE - key_len);
    }

    /* 计算 ipad 和 opad */
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5C;
    }

    /* 内层哈希: SHA256(ipad || data) */
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    /* 外层哈希: SHA256(opad || inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, digest);
}
