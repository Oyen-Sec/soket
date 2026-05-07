
#ifndef MONOCYPHER_H
#define MONOCYPHER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t input[16];
} crypto_chacha20_ctx;

typedef struct {
    uint32_t r[4];
    uint32_t h[5];
    uint32_t pad[4];
    size_t   c;
    uint8_t  buf[16];
} crypto_poly1305_ctx;

typedef struct {
    crypto_chacha20_ctx ctx;
    crypto_poly1305_ctx poly;
} crypto_xchacha20_poly1305_ctx;

typedef struct {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t  buf[128];
    size_t   c;
} crypto_blake2b_ctx;

typedef struct {
    uint8_t pk[32];
} crypto_ed25519_pubkey;

void crypto_wipe(void *secret, size_t size);

void crypto_keygen(uint8_t secret_key[32]);

void crypto_xchacha20_poly1305_encrypt
    (uint8_t       mac[16],
     uint8_t       *cipher_text,
     const uint8_t  key[32],
     const uint8_t  nonce[24],
     const uint8_t *text,
     size_t         text_size,
     const uint8_t *ad,
     size_t         ad_size);

int crypto_xchacha20_poly1305_decrypt
    (uint8_t       *text,
     const uint8_t  key[32],
     const uint8_t  nonce[24],
     const uint8_t  mac[16],
     const uint8_t *cipher_text,
     size_t         text_size,
     const uint8_t *ad,
     size_t         ad_size);

void crypto_x25519(uint8_t       shared_key[32],
                   const uint8_t your_secret_key[32],
                   const uint8_t their_public_key[32]);

void crypto_x25519_public_key(uint8_t       public_key[32],
                              const uint8_t secret_key[32]);

void crypto_blake2b(uint8_t        hash[64],
                    const uint8_t *message, size_t message_size);

void crypto_blake2b_general(uint8_t       *hash   , size_t hash_size,
                            const uint8_t *key    , size_t key_size,
                            const uint8_t *message, size_t message_size);

void crypto_blake2b_init  (crypto_blake2b_ctx *ctx);
void crypto_blake2b_update(crypto_blake2b_ctx *ctx,
                           const uint8_t *message, size_t message_size);
void crypto_blake2b_ (crypto_blake2b_ctx *ctx,
                           uint8_t *hash, size_t hash_size);

#ifdef __cplusplus
}
#endif

#endif
