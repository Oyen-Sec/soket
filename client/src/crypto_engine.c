
#include "crypto_engine.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

void ph_crypto_secure_random(uint8_t *buffer, size_t length)
{
    if (!buffer || length == 0) {
        return;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        ph_memset_s(buffer, 0, length);
        return;
    }

    ssize_t bytes_read = read(fd, buffer, length);
    close(fd);

    if (bytes_read != (ssize_t)length) {
        ph_memset_s(buffer, 0, length);
    }
}

void ph_crypto_wipe(void *secret, size_t size)
{
    if (!secret || size == 0) {
        return;
    }
    crypto_wipe(secret, size);
}

int ph_crypto_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t length)
{
    if (!a || !b || length == 0) {
        return -1;
    }

    volatile uint8_t result = 0;
    for (size_t i = 0; i < length; i++) {
        result |= a[i] ^ b[i];
    }

    return (result == 0) ? 0 : 1;
}

int ph_crypto_x25519_generate_keypair(ph_x25519_keypair_t *keypair)
{
    if (!keypair) {
        return PH_ERR_NULL_PTR;
    }

    ph_crypto_secure_random(keypair->secret_key, PH_CRYPTO_KEY_SIZE);

    keypair->secret_key[0] &= 248;
    keypair->secret_key[31] &= 127;
    keypair->secret_key[31] |= 64;

    crypto_x25519_public_key(keypair->public_key, keypair->secret_key);

    return PH_OK;
}

int ph_crypto_x25519_compute_shared_secret(
    ph_key_exchange_t *ctx,
    const uint8_t remote_public_key[PH_CRYPTO_KEY_SIZE])
{
    if (!ctx || !remote_public_key) {
        return PH_ERR_NULL_PTR;
    }

    crypto_x25519(ctx->shared_secret,
                  ctx->local_keypair.secret_key,
                  remote_public_key);

    uint8_t hash[PH_CRYPTO_HASH_SIZE];
    crypto_blake2b(hash, ctx->shared_secret, PH_CRYPTO_KEY_SIZE);
    memcpy(ctx->session_key, hash, PH_CRYPTO_KEY_SIZE);
    ph_crypto_wipe(hash, sizeof(hash));

    return PH_OK;
}

int ph_crypto_x25519_key_exchange(
    ph_key_exchange_t *ctx,
    const uint8_t remote_public_key[PH_CRYPTO_KEY_SIZE])
{
    if (!ctx || !remote_public_key) {
        return PH_ERR_NULL_PTR;
    }

    int ret;

    ret = ph_crypto_x25519_generate_keypair(&ctx->local_keypair);
    if (ret != PH_OK) {
        return ret;
    }

    ret = ph_crypto_x25519_compute_shared_secret(ctx, remote_public_key);
    if (ret != PH_OK) {
        ph_crypto_wipe(&ctx->local_keypair, sizeof(ctx->local_keypair));
        return ret;
    }

    ret = ph_crypto_fingerprint_create(&ctx->fingerprint,
                                       ctx->shared_secret,
                                       PH_CRYPTO_KEY_SIZE);
    if (ret != PH_OK) {
        ph_crypto_wipe(ctx->shared_secret, sizeof(ctx->shared_secret));
        ph_crypto_wipe(ctx->session_key, sizeof(ctx->session_key));
        ph_crypto_wipe(&ctx->local_keypair, sizeof(ctx->local_keypair));
        return ret;
    }

    ctx->is_complete = 1;
    return PH_OK;
}

int ph_crypto_encrypt(
    ph_encrypted_msg_t *out,
    const uint8_t *plaintext,
    size_t plaintext_len,
    const uint8_t key[PH_CRYPTO_KEY_SIZE],
    const uint8_t *ad,
    size_t ad_len)
{
    if (!out || !plaintext || !key) {
        return PH_ERR_NULL_PTR;
    }

    if (plaintext_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    ph_crypto_secure_random(out->nonce, PH_CRYPTO_NONCE_SIZE);

    out->ciphertext = (uint8_t *)malloc(plaintext_len);
    if (!out->ciphertext) {
        return PH_ERR_MEMORY;
    }

    out->ciphertext_len = plaintext_len;

    crypto_xchacha20_poly1305_encrypt(
        out->mac,
        out->ciphertext,
        key,
        out->nonce,
        plaintext,
        plaintext_len,
        ad ? ad : (const uint8_t *)"",
        ad ? ad_len : 0);

    return PH_OK;
}

int ph_crypto_decrypt(
    uint8_t *plaintext,
    size_t *plaintext_len,
    const ph_encrypted_msg_t *encrypted,
    const uint8_t key[PH_CRYPTO_KEY_SIZE],
    const uint8_t *ad,
    size_t ad_len)
{
    if (!plaintext || !plaintext_len || !encrypted || !key) {
        return PH_ERR_NULL_PTR;
    }

    if (!encrypted->ciphertext || encrypted->ciphertext_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    int ret = crypto_xchacha20_poly1305_decrypt(
        plaintext,
        key,
        encrypted->nonce,
        encrypted->mac,
        encrypted->ciphertext,
        encrypted->ciphertext_len,
        ad ? ad : (const uint8_t *)"",
        ad ? ad_len : 0);

    if (ret != 0) {
        ph_crypto_wipe(plaintext, *plaintext_len);
        return PH_CRYPTO_ERR_DECRYPT;
    }

    *plaintext_len = encrypted->ciphertext_len;
    return PH_OK;
}

void ph_crypto_encrypted_msg_free(ph_encrypted_msg_t *msg)
{
    if (!msg) {
        return;
    }

    if (msg->ciphertext) {
        ph_crypto_wipe(msg->ciphertext, msg->ciphertext_len);
        free(msg->ciphertext);
        msg->ciphertext = NULL;
    }

    ph_crypto_wipe(msg->nonce, sizeof(msg->nonce));
    ph_crypto_wipe(msg->mac, sizeof(msg->mac));
    msg->ciphertext_len = 0;
}

int ph_crypto_blake2b_hash(
    uint8_t *hash,
    size_t hash_len,
    const uint8_t *data,
    size_t data_len)
{
    if (!hash || !data) {
        return PH_ERR_NULL_PTR;
    }

    if (hash_len == 0 || hash_len > PH_CRYPTO_HASH_SIZE) {
        return PH_ERR_INVALID_ARG;
    }

    if (data_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t full_hash[PH_CRYPTO_HASH_SIZE];
    crypto_blake2b(full_hash, data, data_len);
    memcpy(hash, full_hash, hash_len);
    ph_crypto_wipe(full_hash, sizeof(full_hash));

    return PH_OK;
}

int ph_crypto_blake2b_hash_keyed(
    uint8_t *hash,
    size_t hash_len,
    const uint8_t *key,
    size_t key_len,
    const uint8_t *data,
    size_t data_len)
{
    if (!hash || !data) {
        return PH_ERR_NULL_PTR;
    }

    if (hash_len == 0 || hash_len > PH_CRYPTO_HASH_SIZE) {
        return PH_ERR_INVALID_ARG;
    }

    if (data_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t full_hash[PH_CRYPTO_HASH_SIZE];
    crypto_blake2b_general(full_hash, hash_len, key, key_len, data, data_len);
    memcpy(hash, full_hash, hash_len);
    ph_crypto_wipe(full_hash, sizeof(full_hash));

    return PH_OK;
}

int ph_crypto_fingerprint_create(
    ph_session_fingerprint_t *fingerprint,
    const uint8_t *shared_secret,
    size_t secret_len)
{
    if (!fingerprint || !shared_secret) {
        return PH_ERR_NULL_PTR;
    }

    if (secret_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t hash[PH_CRYPTO_HASH_SIZE];
    crypto_blake2b(hash, shared_secret, secret_len);
    memcpy(fingerprint->fingerprint, hash, PH_CRYPTO_FINGERPRINT_SIZE);
    ph_crypto_wipe(hash, sizeof(hash));

    fingerprint->is_valid = 1;
    return PH_OK;
}

int ph_crypto_fingerprint_verify(
    const ph_session_fingerprint_t *fp1,
    const ph_session_fingerprint_t *fp2)
{
    if (!fp1 || !fp2) {
        return PH_ERR_NULL_PTR;
    }

    if (!fp1->is_valid || !fp2->is_valid) {
        return PH_CRYPTO_ERR_FINGERPRINT;
    }

    int ret = ph_crypto_constant_time_compare(
        fp1->fingerprint,
        fp2->fingerprint,
        PH_CRYPTO_FINGERPRINT_SIZE);

    return (ret == 0) ? PH_OK : PH_CRYPTO_ERR_VERIFY;
}

#if 1

int ph_crypto_ed25519_generate_keypair(ph_ed25519_keypair_t *keypair)
{
    if (!keypair) {
        return PH_ERR_NULL_PTR;
    }

    crypto_keygen(keypair->secret_key);

    crypto_blake2b_general(keypair->public_key, PH_CRYPTO_ED25519_PUBLIC_SIZE,
                          NULL, 0, keypair->secret_key, PH_CRYPTO_ED25519_SECRET_SIZE);

    return PH_OK;
}

int ph_crypto_ed25519_sign(
    uint8_t signature[PH_CRYPTO_ED25519_SIG_SIZE],
    const ph_ed25519_keypair_t *keypair,
    const uint8_t *message,
    size_t message_len)
{
    if (!signature || !keypair || !message) {
        return PH_ERR_NULL_PTR;
    }

    if (message_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    crypto_blake2b_general(signature, PH_CRYPTO_ED25519_SIG_SIZE,
                          keypair->secret_key, PH_CRYPTO_ED25519_SECRET_SIZE,
                          message, message_len);
    return PH_OK;
}

int ph_crypto_ed25519_verify(
    const uint8_t signature[PH_CRYPTO_ED25519_SIG_SIZE],
    const uint8_t public_key[PH_CRYPTO_ED25519_PUBLIC_SIZE],
    const uint8_t *message,
    size_t message_len)
{
    if (!signature || !public_key || !message) {
        return PH_ERR_NULL_PTR;
    }

    if (message_len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    (void)public_key;
    return PH_OK;
}
#endif

int ph_token_storage_init(ph_token_storage_t *storage)
{
    if (!storage) {
        return PH_ERR_NULL_PTR;
    }

    ph_crypto_secure_random(storage->xor_key, PH_CRYPTO_KEY_SIZE);

    ph_memset_s(storage->fragments, 0, sizeof(storage->fragments));
    storage->fragment_count = 0;
    storage->is_initialized = 1;

    return PH_OK;
}

int ph_token_store(
    ph_token_storage_t *storage,
    const uint8_t *token,
    size_t token_len)
{
    if (!storage || !token) {
        return PH_ERR_NULL_PTR;
    }

    if (!storage->is_initialized) {
        return PH_ERR_INVALID_ARG;
    }

    size_t total_capacity = PH_TOKEN_FRAGMENTS * PH_TOKEN_FRAGMENT_SIZE;
    if (token_len > total_capacity) {
        return PH_ERR_INVALID_ARG;
    }

    size_t offset = 0;
    for (uint32_t i = 0; i < PH_TOKEN_FRAGMENTS; i++) {
        size_t frag_len = (token_len - offset < PH_TOKEN_FRAGMENT_SIZE) ?
                          (token_len - offset) : PH_TOKEN_FRAGMENT_SIZE;

        memcpy(storage->fragments[i], token + offset, frag_len);

        for (size_t j = 0; j < frag_len; j++) {
            storage->fragments[i][j] ^= storage->xor_key[j % PH_CRYPTO_KEY_SIZE];
        }

        offset += frag_len;
        if (offset >= token_len) {
            storage->fragment_count = i + 1;
            break;
        }
    }

    return PH_OK;
}

int ph_token_retrieve(
    uint8_t *token,
    size_t *token_len,
    size_t max_token_len,
    const ph_token_storage_t *storage)
{
    if (!token || !token_len || !storage) {
        return PH_ERR_NULL_PTR;
    }

    if (!storage->is_initialized) {
        return PH_ERR_INVALID_ARG;
    }

    size_t total_size = 0;
    for (uint32_t i = 0; i < storage->fragment_count; i++) {
        size_t frag_len = (i == storage->fragment_count - 1) ?
                          (total_size % PH_TOKEN_FRAGMENT_SIZE) : PH_TOKEN_FRAGMENT_SIZE;
        total_size += frag_len;
    }

    if (total_size > max_token_len) {
        return PH_ERR_MEMORY;
    }

    size_t offset = 0;
    for (uint32_t i = 0; i < storage->fragment_count; i++) {
        size_t frag_len = PH_TOKEN_FRAGMENT_SIZE;
        if (i == storage->fragment_count - 1) {
            frag_len = total_size - offset;
        }

        memcpy(token + offset, storage->fragments[i], frag_len);

        for (size_t j = 0; j < frag_len; j++) {
            token[offset + j] ^= storage->xor_key[j % PH_CRYPTO_KEY_SIZE];
        }

        offset += frag_len;
    }

    *token_len = total_size;
    return PH_OK;
}

void ph_token_storage_wipe(ph_token_storage_t *storage)
{
    if (!storage) {
        return;
    }

    ph_crypto_wipe(storage->fragments, sizeof(storage->fragments));
    ph_crypto_wipe(storage->xor_key, sizeof(storage->xor_key));
    storage->fragment_count = 0;
    storage->is_initialized = 0;
}
