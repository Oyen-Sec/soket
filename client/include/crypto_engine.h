
#ifndef CRYPTO_ENGINE_H
#define CRYPTO_ENGINE_H

#include "phantom.h"
#include "monocypher.h"
#include <stdint.h>
#include <stddef.h>

#define PH_CRYPTO_KEY_SIZE 32
#define PH_CRYPTO_NONCE_SIZE 24
#define PH_CRYPTO_MAC_SIZE 16
#define PH_CRYPTO_HASH_SIZE 64
#define PH_CRYPTO_FINGERPRINT_SIZE 16
#define PH_CRYPTO_ED25519_SECRET_SIZE 64
#define PH_CRYPTO_ED25519_PUBLIC_SIZE 32
#define PH_CRYPTO_ED25519_SIG_SIZE 64

static const uint8_t PH_RELAY_PUB_KEY[32] = {
    0x48, 0x45, 0x32, 0x2D, 0x3B, 0x0B, 0x6E, 0x58, 
    0xE0, 0xB3, 0x59, 0x15, 0x7F, 0x8B, 0x31, 0xB8, 
    0x2A, 0xCF, 0xE9, 0x85, 0x04, 0x52, 0xD8, 0x76, 
    0x00, 0x0A, 0x94, 0x43, 0x32, 0x75, 0x02, 0x44
};

#define PH_TOKEN_FRAGMENTS 3
#define PH_TOKEN_FRAGMENT_SIZE 64

#define PH_CRYPTO_ERR_KEYGEN -10
#define PH_CRYPTO_ERR_ENCRYPT -11
#define PH_CRYPTO_ERR_DECRYPT -12
#define PH_CRYPTO_ERR_SIGN -13
#define PH_CRYPTO_ERR_VERIFY -14
#define PH_CRYPTO_ERR_HASH -15
#define PH_CRYPTO_ERR_FINGERPRINT -16

typedef struct {
    uint8_t secret_key[PH_CRYPTO_KEY_SIZE];
    uint8_t public_key[PH_CRYPTO_KEY_SIZE];
} ph_x25519_keypair_t;

typedef struct {
    uint8_t secret_key[PH_CRYPTO_ED25519_SECRET_SIZE];
    uint8_t public_key[PH_CRYPTO_ED25519_PUBLIC_SIZE];
} ph_ed25519_keypair_t;

typedef struct {
    uint8_t nonce[PH_CRYPTO_NONCE_SIZE];
    uint8_t mac[PH_CRYPTO_MAC_SIZE];
    uint8_t *ciphertext;
    size_t ciphertext_len;
} ph_encrypted_msg_t;

typedef struct {
    uint8_t fragments[PH_TOKEN_FRAGMENTS][PH_TOKEN_FRAGMENT_SIZE];
    uint8_t xor_key[PH_CRYPTO_KEY_SIZE];
    uint32_t fragment_count;
    int is_initialized;
} ph_token_storage_t;

typedef struct {
    uint8_t fingerprint[PH_CRYPTO_FINGERPRINT_SIZE];
    int is_valid;
} ph_session_fingerprint_t;

typedef struct {
    ph_x25519_keypair_t local_keypair;
    uint8_t shared_secret[PH_CRYPTO_KEY_SIZE];
    uint8_t session_key[PH_CRYPTO_KEY_SIZE];
    ph_session_fingerprint_t fingerprint;
    int is_complete;
} ph_key_exchange_t;

int ph_crypto_x25519_generate_keypair(ph_x25519_keypair_t *keypair);
int ph_crypto_x25519_compute_shared_secret(
    ph_key_exchange_t *ctx,
    const uint8_t remote_public_key[PH_CRYPTO_KEY_SIZE]);
int ph_crypto_x25519_key_exchange(
    ph_key_exchange_t *ctx,
    const uint8_t remote_public_key[PH_CRYPTO_KEY_SIZE]);

int ph_crypto_encrypt(
    ph_encrypted_msg_t *out,
    const uint8_t *plaintext,
    size_t plaintext_len,
    const uint8_t key[PH_CRYPTO_KEY_SIZE],
    const uint8_t *ad,
    size_t ad_len);

int ph_crypto_decrypt(
    uint8_t *plaintext,
    size_t *plaintext_len,
    const ph_encrypted_msg_t *encrypted,
    const uint8_t key[PH_CRYPTO_KEY_SIZE],
    const uint8_t *ad,
    size_t ad_len);

void ph_crypto_encrypted_msg_free(ph_encrypted_msg_t *msg);

int ph_crypto_blake2b_hash(
    uint8_t *hash,
    size_t hash_len,
    const uint8_t *data,
    size_t data_len);

int ph_crypto_blake2b_hash_keyed(
    uint8_t *hash,
    size_t hash_len,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *data,
    size_t data_len);

int ph_crypto_fingerprint_create(
    ph_session_fingerprint_t *fingerprint,
    const uint8_t *shared_secret,
    size_t secret_len);

int ph_crypto_fingerprint_verify(
    const ph_session_fingerprint_t *fp1,
    const ph_session_fingerprint_t *fp2);

int ph_crypto_ed25519_generate_keypair(ph_ed25519_keypair_t *keypair);
int ph_crypto_ed25519_sign(
    uint8_t signature[PH_CRYPTO_ED25519_SIG_SIZE],
    const ph_ed25519_keypair_t *keypair,
    const uint8_t *message,
    size_t message_len);

int ph_crypto_ed25519_verify(
    const uint8_t signature[PH_CRYPTO_ED25519_SIG_SIZE],
    const uint8_t public_key[PH_CRYPTO_ED25519_PUBLIC_SIZE],
    const uint8_t *message,
    size_t message_len);

int ph_token_storage_init(ph_token_storage_t *storage);
int ph_token_store(
    ph_token_storage_t *storage,
    const uint8_t *token,
    size_t token_len);

int ph_token_retrieve(
    uint8_t *token,
    size_t *token_len,
    size_t max_token_len,
    const ph_token_storage_t *storage);

void ph_token_storage_wipe(ph_token_storage_t *storage);

void ph_crypto_secure_random(uint8_t *buffer, size_t length);
void ph_crypto_wipe(void *secret, size_t size);
int ph_crypto_constant_time_compare(
    const uint8_t *a,
    const uint8_t *b,
    size_t length);

#endif
