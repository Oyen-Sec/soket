
#include "turn_client.h"
#include "dns_resolver.h"
#include "crypto_engine.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>

#define XOR_KEY 0xAB

static const uint8_t obf_turn1[] = {
    0x8E, 0x9F, 0x9A, 0x8F, 0xA6, 0x8A, 0x9B, 0x8A, 0x8A, 0x9E, 0x9A, 0x8A, 0x8C, 0x8E
};

static const uint8_t obf_turn2[] = {
    0x8E, 0x9F, 0x9A, 0x8F, 0xB2, 0xA6, 0x8A, 0x9B, 0x8A, 0x8A, 0x9E, 0x9A, 0x8A, 0x8C, 0x8E
};

static void turn_decode_string(char *dest, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dest[i] = (char)(src[i] ^ XOR_KEY);
    }
    dest[len] = '\0';
}

int turn_client_init(turn_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    memset(client, 0, sizeof(turn_client_t));
    client->server_count = 0;
    client->current_server = 0;
    client->timeout_ms = TURN_TIMEOUT_MS;
    client->max_retries = TURN_MAX_RETRIES;
    client->transport = TURN_TRANSPORT_UDP;
    client->is_initialized = 1;

    turn_allocation_clear(&client->allocation);

    return PH_OK;
}

int turn_client_set_credentials(turn_client_t *client,
                                 const char *username,
                                 const char *password)
{
    if (!client || !username || !password) {
        return PH_ERR_NULL_PTR;
    }

    strncpy(client->credentials.username, username,
            sizeof(client->credentials.username) - 1);
    strncpy(client->credentials.password, password,
            sizeof(client->credentials.password) - 1);
    client->credentials.has_credentials = 1;

    return PH_OK;
}

int turn_client_add_server(turn_client_t *client,
                           const char *address,
                           uint16_t port,
                           uint8_t transport)
{
    if (!client || !address) {
        return PH_ERR_NULL_PTR;
    }

    if (client->server_count >= TURN_MAX_SERVERS) {
        return PH_ERR_INVALID_ARG;
    }

    strncpy(client->servers[client->server_count].address,
            address, sizeof(client->servers[0].address) - 1);
    client->servers[client->server_count].port = port;
    client->servers[client->server_count].transport = transport;
    client->servers[client->server_count].is_primary = (client->server_count == 0);
    client->server_count++;

    return PH_OK;
}

int turn_client_add_default_servers(turn_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    char server_name[64];

    turn_decode_string(server_name, obf_turn1, sizeof(obf_turn1));
    turn_client_add_server(client, server_name, TURN_PORT,
                           TURN_TRANSPORT_UDP);
    turn_decode_string(server_name, obf_turn2, sizeof(obf_turn2));
    turn_client_add_server(client, server_name, TURN_PORT,
                           TURN_TRANSPORT_UDP);

    return PH_OK;
}

void turn_allocation_clear(turn_allocation_t *alloc)
{
    if (!alloc) {
        return;
    }

    memset(alloc, 0, sizeof(turn_allocation_t));
    alloc->state = TURN_ALLOC_STATE_IDLE;
    alloc->socket_fd = -1;
    alloc->lifetime = TURN_ALLOCATE_LIFETIME;
}

void turn_client_cleanup(turn_client_t *client)
{
    if (!client) {
        return;
    }

    turn_free_allocation(client);
    memset(client, 0, sizeof(turn_client_t));
    client->is_initialized = 0;
}

const char* turn_state_string(turn_alloc_state_t state)
{
    switch (state) {
        case TURN_ALLOC_STATE_IDLE:
            return "Idle";
        case TURN_ALLOC_STATE_ALLOCATING:
            return "Allocating";
        case TURN_ALLOC_STATE_ALLOCATED:
            return "Allocated";
        case TURN_ALLOC_STATE_REFRESHING:
            return "Refreshing";
        case TURN_ALLOC_STATE_EXPIRED:
            return "Expired";
        case TURN_ALLOC_STATE_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

uint16_t turn_allocate_channel_number(void)
{

    uint16_t channel = TURN_CHANNEL_MIN + (rand() % (TURN_CHANNEL_MAX - TURN_CHANNEL_MIN + 1));
    return channel;
}

int turn_is_valid_channel(uint16_t channel)
{
    return (channel >= TURN_CHANNEL_MIN && channel <= TURN_CHANNEL_MAX);
}

int turn_compute_message_integrity(uint8_t *hmac,
                                    const uint8_t *message,
                                    size_t message_len,
                                    const char *password)
{
    if (!hmac || !message || !password) {
        return PH_ERR_NULL_PTR;
    }

    uint8_t key[32];
    ph_crypto_blake2b_hash(key, sizeof(key),
                           (const uint8_t *)password,
                           strlen(password));

    ph_crypto_blake2b_hash_keyed(hmac, TURN_HMAC_SIZE,
                                  key, sizeof(key),
                                  message, message_len);

    return PH_OK;
}

int turn_add_authentication(uint8_t *buffer,
                             size_t buffer_len,
                             int msg_len,
                             const turn_credentials_t *creds)
{
    if (!buffer || !creds || !creds->has_credentials) {
        return PH_ERR_NULL_PTR;
    }

    uint8_t *ptr = buffer + msg_len;
    size_t remaining = buffer_len - msg_len;

    if (remaining < 6 + strlen(creds->username)) {
        return PH_ERR_INVALID_ARG;
    }

    uint16_t username_len = strlen(creds->username);
    *ptr++ = 0x00;
    *ptr++ = 0x06;
    *ptr++ = (username_len >> 8) & 0xFF;
    *ptr++ = username_len & 0xFF;
    memcpy(ptr, creds->username, username_len);
    ptr += username_len;

    while ((ptr - buffer) % 4 != 0) {
        *ptr++ = 0x00;
    }

    if (strlen(creds->nonce) > 0) {
        uint16_t nonce_len = (uint16_t)strlen(creds->nonce);
        if (remaining < (size_t)(6 + nonce_len)) {
            return PH_ERR_INVALID_ARG;
        }

        *ptr++ = 0x00;
        *ptr++ = 0x15;
        *ptr++ = (nonce_len >> 8) & 0xFF;
        *ptr++ = nonce_len & 0xFF;
        memcpy(ptr, creds->nonce, nonce_len);
        ptr += nonce_len;

        while ((ptr - buffer) % 4 != 0) {
            *ptr++ = 0x00;
        }
    }

    if (strlen(creds->realm) > 0) {
        uint16_t realm_len = (uint16_t)strlen(creds->realm);
        if (remaining < (size_t)(6 + realm_len)) {
            return PH_ERR_INVALID_ARG;
        }

        *ptr++ = 0x00;
        *ptr++ = 0x14;
        *ptr++ = (realm_len >> 8) & 0xFF;
        *ptr++ = realm_len & 0xFF;
        memcpy(ptr, creds->realm, realm_len);
        ptr += realm_len;

        while ((ptr - buffer) % 4 != 0) {
            *ptr++ = 0x00;
        }
    }

    return (int)(ptr - buffer - msg_len);
}

int turn_build_allocate_request(uint8_t *buffer,
                                 size_t buffer_len,
                                 uint8_t *transaction_id,
                                 uint32_t lifetime)
{
    if (!buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = 0x00;
    *ptr++ = 0x03;

    uint8_t *msg_len_ptr = ptr;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    uint32_t magic = PH_STUN_MAGIC_COOKIE;
    *ptr++ = (magic >> 24) & 0xFF;
    *ptr++ = (magic >> 16) & 0xFF;
    *ptr++ = (magic >> 8) & 0xFF;
    *ptr++ = magic & 0xFF;

    if (transaction_id) {
        memcpy(ptr, transaction_id, PH_STUN_TRANSACTION_ID_SIZE);
    } else {
        memset(ptr, 0, PH_STUN_TRANSACTION_ID_SIZE);
    }
    ptr += PH_STUN_TRANSACTION_ID_SIZE;

    *ptr++ = 0x00;
    *ptr++ = 0x19;
    *ptr++ = 0x00;
    *ptr++ = 0x04;
    *ptr++ = TURN_TRANSPORT_UDP;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    if (lifetime > 0) {
        *ptr++ = 0x00;
        *ptr++ = 0x0D;
        *ptr++ = 0x00;
        *ptr++ = 0x04;
        *ptr++ = (lifetime >> 24) & 0xFF;
        *ptr++ = (lifetime >> 16) & 0xFF;
        *ptr++ = (lifetime >> 8) & 0xFF;
        *ptr++ = lifetime & 0xFF;
    }

    uint16_t msg_len = ptr - buffer - 20;
    msg_len_ptr[0] = (msg_len >> 8) & 0xFF;
    msg_len_ptr[1] = msg_len & 0xFF;

    return (int)(ptr - buffer);
}

int turn_build_refresh_request(uint8_t *buffer,
                                size_t buffer_len,
                                uint8_t *transaction_id,
                                uint32_t lifetime)
{
    if (!buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = 0x00;
    *ptr++ = 0x04;

    uint8_t *msg_len_ptr = ptr;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    uint32_t magic = PH_STUN_MAGIC_COOKIE;
    *ptr++ = (magic >> 24) & 0xFF;
    *ptr++ = (magic >> 16) & 0xFF;
    *ptr++ = (magic >> 8) & 0xFF;
    *ptr++ = magic & 0xFF;

    if (transaction_id) {
        memcpy(ptr, transaction_id, PH_STUN_TRANSACTION_ID_SIZE);
    } else {
        memset(ptr, 0, PH_STUN_TRANSACTION_ID_SIZE);
    }
    ptr += PH_STUN_TRANSACTION_ID_SIZE;

    *ptr++ = 0x00;
    *ptr++ = 0x0D;
    *ptr++ = 0x00;
    *ptr++ = 0x04;
    *ptr++ = (lifetime >> 24) & 0xFF;
    *ptr++ = (lifetime >> 16) & 0xFF;
    *ptr++ = (lifetime >> 8) & 0xFF;
    *ptr++ = lifetime & 0xFF;

    uint16_t msg_len = ptr - buffer - 20;
    msg_len_ptr[0] = (msg_len >> 8) & 0xFF;
    msg_len_ptr[1] = msg_len & 0xFF;

    return (int)(ptr - buffer);
}

int turn_build_permission_request(uint8_t *buffer,
                                   size_t buffer_len,
                                   uint8_t *transaction_id,
                                   const char *peer_ip)
{
    if (!buffer || !peer_ip || buffer_len < 28) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = 0x00;
    *ptr++ = 0x08;

    uint8_t *msg_len_ptr = ptr;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    uint32_t magic = PH_STUN_MAGIC_COOKIE;
    *ptr++ = (magic >> 24) & 0xFF;
    *ptr++ = (magic >> 16) & 0xFF;
    *ptr++ = (magic >> 8) & 0xFF;
    *ptr++ = magic & 0xFF;

    if (transaction_id) {
        memcpy(ptr, transaction_id, PH_STUN_TRANSACTION_ID_SIZE);
    } else {
        memset(ptr, 0, PH_STUN_TRANSACTION_ID_SIZE);
    }
    ptr += PH_STUN_TRANSACTION_ID_SIZE;

    *ptr++ = 0x00;
    *ptr++ = 0x12;
    *ptr++ = 0x00;
    *ptr++ = 0x08;
    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    struct in_addr addr;
    inet_pton(AF_INET, peer_ip, &addr);
    uint32_t xor_ip = addr.s_addr ^ PH_STUN_MAGIC_COOKIE;
    memcpy(ptr, &xor_ip, 4);
    ptr += 4;

    uint16_t msg_len = ptr - buffer - 20;
    msg_len_ptr[0] = (msg_len >> 8) & 0xFF;
    msg_len_ptr[1] = msg_len & 0xFF;

    return (int)(ptr - buffer);
}

int turn_build_channel_bind_request(uint8_t *buffer,
                                     size_t buffer_len,
                                     uint8_t *transaction_id,
                                     uint16_t channel_number,
                                     const char *peer_ip)
{
    if (!buffer || !peer_ip || buffer_len < 32) {
        return PH_ERR_INVALID_ARG;
    }

    if (!turn_is_valid_channel(channel_number)) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = 0x00;
    *ptr++ = 0x09;

    uint8_t *msg_len_ptr = ptr;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    uint32_t magic = PH_STUN_MAGIC_COOKIE;
    *ptr++ = (magic >> 24) & 0xFF;
    *ptr++ = (magic >> 16) & 0xFF;
    *ptr++ = (magic >> 8) & 0xFF;
    *ptr++ = magic & 0xFF;

    if (transaction_id) {
        memcpy(ptr, transaction_id, PH_STUN_TRANSACTION_ID_SIZE);
    } else {
        memset(ptr, 0, PH_STUN_TRANSACTION_ID_SIZE);
    }
    ptr += PH_STUN_TRANSACTION_ID_SIZE;

    *ptr++ = 0x00;
    *ptr++ = 0x0C;
    *ptr++ = 0x00;
    *ptr++ = 0x04;
    *ptr++ = (channel_number >> 8) & 0xFF;
    *ptr++ = channel_number & 0xFF;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    *ptr++ = 0x00;
    *ptr++ = 0x12;
    *ptr++ = 0x00;
    *ptr++ = 0x08;
    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = 0x00;
    *ptr++ = 0x00;

    struct in_addr addr;
    inet_pton(AF_INET, peer_ip, &addr);
    uint32_t xor_ip = addr.s_addr ^ PH_STUN_MAGIC_COOKIE;
    memcpy(ptr, &xor_ip, 4);
    ptr += 4;

    uint16_t msg_len = ptr - buffer - 20;
    msg_len_ptr[0] = (msg_len >> 8) & 0xFF;
    msg_len_ptr[1] = msg_len & 0xFF;

    return (int)(ptr - buffer);
}

int turn_build_send_indication(uint8_t *buffer,
                                size_t buffer_len,
                                uint16_t channel_number,
                                const uint8_t *data,
                                size_t data_len)
{
    if (!buffer || !data || buffer_len < 4 + data_len) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = (channel_number >> 8) & 0xFF;
    *ptr++ = channel_number & 0xFF;

    *ptr++ = (data_len >> 8) & 0xFF;
    *ptr++ = data_len & 0xFF;

    memcpy(ptr, data, data_len);
    ptr += data_len;

    return (int)(ptr - buffer);
}

int turn_parse_allocate_response(turn_allocation_t *alloc,
                                  const uint8_t *buffer,
                                  size_t buffer_len)
{
    if (!alloc || !buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    const uint8_t *ptr = buffer;

    uint16_t msg_type = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    if (msg_type != TURN_ALLOCATE_SUCCESS) {

        if (msg_type == 0x0113) {
            return TURN_ERR_ALLOCATE;
        }
        return PH_ERR_INVALID_ARG;
    }

    uint16_t msg_len = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    ptr += 16;

    const uint8_t *attr_ptr = ptr;
    const uint8_t *attr_end = ptr + msg_len;

    while (attr_ptr < attr_end - 4) {
        uint16_t attr_type = (attr_ptr[0] << 8) | attr_ptr[1];
        uint16_t attr_len = (attr_ptr[2] << 8) | attr_ptr[3];
        attr_ptr += 4;

        if (attr_ptr + attr_len > attr_end) {
            break;
        }

        if (attr_type == TURN_ATTR_XOR_RELAYED_ADDRESS) {
            if (attr_len < 8) {
                continue;
            }

            uint8_t family = attr_ptr[1];
            uint16_t port = (attr_ptr[2] << 8) | attr_ptr[3];

            port ^= (PH_STUN_MAGIC_COOKIE >> 16) & 0xFFFF;

            if (family == 0x01) {

                uint32_t ip_addr;
                memcpy(&ip_addr, attr_ptr + 4, 4);
                ip_addr ^= PH_STUN_MAGIC_COOKIE;

                struct in_addr addr;
                addr.s_addr = ip_addr;
                inet_ntop(AF_INET, &addr, alloc->relayed_ip,
                          sizeof(alloc->relayed_ip));
                alloc->relayed_port = port;
            }
        }

        if (attr_type == TURN_ATTR_LIFETIME) {
            if (attr_len >= 4) {
                alloc->lifetime = (attr_ptr[0] << 24) | (attr_ptr[1] << 16) |
                                  (attr_ptr[2] << 8) | attr_ptr[3];
                alloc->time_remaining = alloc->lifetime;
                alloc->last_refresh = ph_get_timestamp_ms();
            }
        }

        if (attr_type == TURN_ATTR_NONCE) {
            if (attr_len < TURN_NONCE_SIZE) {

            }
        }

        attr_ptr += attr_len;
        if (attr_len % 4 != 0) {
            attr_ptr += 4 - (attr_len % 4);
        }
    }

    alloc->state = TURN_ALLOC_STATE_ALLOCATED;

    return PH_OK;
}

int turn_parse_refresh_response(turn_allocation_t *alloc,
                                 const uint8_t *buffer,
                                 size_t buffer_len)
{
    if (!alloc || !buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    const uint8_t *ptr = buffer;

    uint16_t msg_type = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    if (msg_type != TURN_REFRESH_SUCCESS) {
        return TURN_ERR_REFRESH;
    }

    uint16_t msg_len = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    ptr += 16;

    const uint8_t *attr_ptr = ptr;
    const uint8_t *attr_end = ptr + msg_len;

    while (attr_ptr < attr_end - 4) {
        uint16_t attr_type = (attr_ptr[0] << 8) | attr_ptr[1];
        uint16_t attr_len = (attr_ptr[2] << 8) | attr_ptr[3];
        attr_ptr += 4;

        if (attr_ptr + attr_len > attr_end) {
            break;
        }

        if (attr_type == TURN_ATTR_LIFETIME) {
            if (attr_len >= 4) {
                alloc->lifetime = (attr_ptr[0] << 24) | (attr_ptr[1] << 16) |
                                  (attr_ptr[2] << 8) | attr_ptr[3];
                alloc->time_remaining = alloc->lifetime;
                alloc->last_refresh = ph_get_timestamp_ms();
            }
        }

        attr_ptr += attr_len;
        if (attr_len % 4 != 0) {
            attr_ptr += 4 - (attr_len % 4);
        }
    }

    return PH_OK;
}

int turn_parse_error_response(const uint8_t *buffer,
                               size_t buffer_len,
                               char *error_msg,
                               size_t error_len)
{
    if (!buffer || !error_msg || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    const char *msg = "TURN err";
    size_t len = 8;
    if (len >= error_len) {
        len = error_len - 1;
    }
    memcpy(error_msg, msg, len);
    error_msg[len] = '\0';

    return TURN_ERR_NETWORK;
}

static int turn_parse_401_response(turn_credentials_t *creds,
                                    const uint8_t *buffer,
                                    size_t buffer_len)
{
    if (!creds || !buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    const uint8_t *ptr = buffer;

    uint16_t msg_type = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    if (msg_type != 0x0113 && msg_type != 0x0111) {
        return PH_ERR_INVALID_ARG;
    }

    uint16_t msg_len = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    ptr += 16;

    const uint8_t *attr_ptr = ptr;
    const uint8_t *attr_end = ptr + msg_len;
    int found_nonce = 0;
    int found_realm = 0;

    while (attr_ptr < attr_end - 4) {
        uint16_t attr_type = (attr_ptr[0] << 8) | attr_ptr[1];
        uint16_t attr_len = (attr_ptr[2] << 8) | attr_ptr[3];
        attr_ptr += 4;

        if (attr_ptr + attr_len > attr_end) {
            break;
        }

        if (attr_type == TURN_ATTR_NONCE && attr_len < TURN_NONCE_SIZE) {
            memcpy(creds->nonce, attr_ptr, attr_len);
            creds->nonce[attr_len] = '\0';
            found_nonce = 1;
        }

        if (attr_type == TURN_ATTR_REALM && attr_len < TURN_REALM_SIZE) {
            memcpy(creds->realm, attr_ptr, attr_len);
            creds->realm[attr_len] = '\0';
            found_realm = 1;
        }

        attr_ptr += attr_len;
        if (attr_len % 4 != 0) {
            attr_ptr += 4 - (attr_len % 4);
        }
    }

    if (found_nonce && found_realm) {
        return PH_OK;
    }

    return PH_ERR_INVALID_ARG;
}

static int turn_allocate_request_auth(turn_allocation_t *alloc,
                                       turn_client_t *client,
                                       int server_index)
{
    if (!alloc || !client) {
        return PH_ERR_NULL_PTR;
    }

    if (!client->credentials.has_credentials) {
        return TURN_ERR_AUTH;
    }

    uint8_t request[TURN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = turn_build_allocate_request(request, sizeof(request),
                                               transaction_id,
                                               TURN_ALLOCATE_LIFETIME);
    if (req_len < 0) {
        return PH_ERR_INVALID_ARG;
    }

    int auth_len = turn_add_authentication(request, sizeof(request),
                                            req_len,
                                            &client->credentials);
    if (auth_len < 0) {
        return PH_ERR_INVALID_ARG;
    }

    req_len += auth_len;

    ph_dns_config_t dns_config;
    ph_dns_config_init(&dns_config);
    ph_dns_config_parse_resolvconf(&dns_config, "/etc/resolv.conf");
    ph_dns_config_detect_systemd_resolved(&dns_config);

    ph_dns_result_t dns_result;
    int ret = ph_dns_resolve(&dns_result,
                             client->servers[server_index].address,
                             &dns_config);
    if (ret != PH_OK || !dns_result.success) {
        return PH_ERR_DNS;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return PH_ERR_SOCKET;
    }

    struct timeval tv;
    tv.tv_sec = client->timeout_ms / 1000;
    tv.tv_usec = (client->timeout_ms % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[server_index].port);
    inet_pton(AF_INET, dns_result.ip_address, &server_addr.sin_addr);

    ssize_t sent = sendto(sockfd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        close(sockfd);
        return PH_ERR_NETWORK;
    }

    uint8_t response[TURN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(sockfd, response, sizeof(response), 0,
                                (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        close(sockfd);
        return TURN_ERR_TIMEOUT;
    }

    ret = turn_parse_allocate_response(alloc, response, recv_len);

    if (ret == PH_OK) {
        alloc->socket_fd = sockfd;
        alloc->state = TURN_ALLOC_STATE_ALLOCATED;
        alloc->retry_count = 0;
    } else {
        close(sockfd);
        alloc->state = TURN_ALLOC_STATE_ERROR;
    }

    return ret;
}

int turn_allocate(turn_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    if (client->server_count == 0) {
        return PH_ERR_INVALID_ARG;
    }

    for (int i = 0; i < client->server_count; i++) {
        int ret = turn_allocate_request(&client->allocation, client, i);
        if (ret == PH_OK &&
            client->allocation.state == TURN_ALLOC_STATE_ALLOCATED) {
            return PH_OK;
        }
    }

    client->allocation.state = TURN_ALLOC_STATE_ERROR;

    return TURN_ERR_ALLOCATE;
}

int turn_allocate_request(turn_allocation_t *alloc,
                          turn_client_t *client,
                          int server_index)
{
    if (!alloc || !client) {
        return PH_ERR_NULL_PTR;
    }

    if (server_index < 0 || server_index >= client->server_count) {
        return PH_ERR_INVALID_ARG;
    }

    turn_allocation_clear(alloc);
    alloc->state = TURN_ALLOC_STATE_ALLOCATING;

    ph_dns_config_t dns_config;
    ph_dns_config_init(&dns_config);
    ph_dns_config_parse_resolvconf(&dns_config, "/etc/resolv.conf");
    ph_dns_config_detect_systemd_resolved(&dns_config);

    ph_dns_result_t dns_result;
    int ret = ph_dns_resolve(&dns_result,
                             client->servers[server_index].address,
                             &dns_config);
    if (ret != PH_OK || !dns_result.success) {
        return PH_ERR_DNS;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return PH_ERR_SOCKET;
    }

    struct timeval tv;
    tv.tv_sec = client->timeout_ms / 1000;
    tv.tv_usec = (client->timeout_ms % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[server_index].port);
    inet_pton(AF_INET, dns_result.ip_address, &server_addr.sin_addr);

    uint8_t request[TURN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = turn_build_allocate_request(request, sizeof(request),
                                               transaction_id,
                                               TURN_ALLOCATE_LIFETIME);
    if (req_len < 0) {
        close(sockfd);
        return PH_ERR_INVALID_ARG;
    }

    ssize_t sent = sendto(sockfd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        close(sockfd);
        return PH_ERR_NETWORK;
    }

    uint8_t response[TURN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(sockfd, response, sizeof(response), 0,
                                (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
        } else {
        }
        close(sockfd);
        return TURN_ERR_TIMEOUT;
    }

    ret = turn_parse_allocate_response(alloc, response, recv_len);

    if (ret == PH_OK) {
        alloc->socket_fd = sockfd;
        alloc->state = TURN_ALLOC_STATE_ALLOCATED;
        alloc->retry_count = 0;
    } else if (ret == TURN_ERR_ALLOCATE) {

        uint16_t msg_type = (response[0] << 8) | response[1];
        if (msg_type == 0x0113) {

            int parse_ret = turn_parse_401_response(&client->credentials,
                                                     response, recv_len);
            if (parse_ret == PH_OK && client->credentials.has_credentials) {
                close(sockfd);

                ret = turn_allocate_request_auth(alloc, client, server_index);
            } else {
                close(sockfd);
                alloc->state = TURN_ALLOC_STATE_ERROR;
            }
        } else {
            close(sockfd);
            alloc->state = TURN_ALLOC_STATE_ERROR;
        }
    } else {
        close(sockfd);
        alloc->state = TURN_ALLOC_STATE_ERROR;
    }

    return ret;
}

int turn_refresh_allocation(turn_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    if (client->allocation.state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    uint64_t now = ph_get_timestamp_ms();
    uint64_t elapsed = (now - client->allocation.last_refresh) / 1000;

    if (elapsed < TURN_REFRESH_INTERVAL) {
        return PH_OK;
    }

    client->allocation.state = TURN_ALLOC_STATE_REFRESHING;

    uint8_t request[TURN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = turn_build_refresh_request(request, sizeof(request),
                                              transaction_id,
                                              TURN_ALLOCATE_LIFETIME);
    if (req_len < 0) {
        client->allocation.state = TURN_ALLOC_STATE_ERROR;
        return PH_ERR_INVALID_ARG;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[client->current_server].port);

    ph_dns_config_t dns_config;
    ph_dns_config_init(&dns_config);
    ph_dns_config_parse_resolvconf(&dns_config, "/etc/resolv.conf");

    ph_dns_result_t dns_result;
    int ret = ph_dns_resolve(&dns_result,
                             client->servers[client->current_server].address,
                             &dns_config);
    if (ret == PH_OK && dns_result.success) {
        inet_pton(AF_INET, dns_result.ip_address, &server_addr.sin_addr);
    }

    ssize_t sent = sendto(client->allocation.socket_fd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        client->allocation.state = TURN_ALLOC_STATE_ERROR;
        return PH_ERR_NETWORK;
    }

    uint8_t response[TURN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(client->allocation.socket_fd, response,
                                 sizeof(response), 0,
                                 (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        client->allocation.state = TURN_ALLOC_STATE_ERROR;
        return TURN_ERR_TIMEOUT;
    }

    ret = turn_parse_refresh_response(&client->allocation, response, recv_len);

    if (ret == PH_OK) {
        client->allocation.state = TURN_ALLOC_STATE_ALLOCATED;
    } else {
        client->allocation.state = TURN_ALLOC_STATE_ERROR;
    }

    return ret;
}

int turn_free_allocation(turn_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    if (client->allocation.socket_fd >= 0) {

        uint8_t request[TURN_MAX_RESPONSE];
        uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
        ph_stun_generate_transaction_id(transaction_id);

        int req_len = turn_build_refresh_request(request, sizeof(request),
                                                  transaction_id, 0);
        if (req_len >= 0) {

            send(client->allocation.socket_fd, request, req_len, 0);
        }

        close(client->allocation.socket_fd);
        client->allocation.socket_fd = -1;
    }

    turn_allocation_clear(&client->allocation);

    return PH_OK;
}

int turn_create_permission(turn_client_t *client,
                           const char *peer_ip,
                           uint16_t peer_port)
{
    if (!client || !peer_ip) {
        return PH_ERR_NULL_PTR;
    }

    (void)peer_port;

    if (client->allocation.state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t request[TURN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = turn_build_permission_request(request, sizeof(request),
                                                 transaction_id, peer_ip);
    if (req_len < 0) {
        return PH_ERR_INVALID_ARG;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[client->current_server].port);

    ssize_t sent = sendto(client->allocation.socket_fd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    uint8_t response[TURN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(client->allocation.socket_fd, response,
                                 sizeof(response), 0,
                                 (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        return TURN_ERR_TIMEOUT;
    }

    uint16_t msg_type = (response[0] << 8) | response[1];
    if (msg_type == TURN_CREATE_PERMISSION_SUCCESS) {
        return PH_OK;
    }

    return TURN_ERR_PERMISSION;
}

int turn_bind_channel(turn_client_t *client,
                      turn_peer_t *peer)
{
    if (!client || !peer) {
        return PH_ERR_NULL_PTR;
    }

    if (client->allocation.state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    peer->channel_number = turn_allocate_channel_number();

    uint8_t request[TURN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = turn_build_channel_bind_request(request, sizeof(request),
                                                   transaction_id,
                                                   peer->channel_number,
                                                   peer->peer_ip);
    if (req_len < 0) {
        return PH_ERR_INVALID_ARG;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[client->current_server].port);

    ssize_t sent = sendto(client->allocation.socket_fd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    uint8_t response[TURN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(client->allocation.socket_fd, response,
                                 sizeof(response), 0,
                                 (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        return TURN_ERR_TIMEOUT;
    }

    uint16_t msg_type = (response[0] << 8) | response[1];
    if (msg_type == TURN_CHANNEL_BIND_SUCCESS) {
        peer->has_permission = 1;
        return PH_OK;
    }

    return TURN_ERR_CHANNEL;
}

int turn_send_data(turn_client_t *client,
                   turn_peer_t *peer,
                   const uint8_t *data,
                   size_t data_len)
{
    if (!client || !peer || !data) {
        return PH_ERR_NULL_PTR;
    }

    if (client->allocation.state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    if (!peer->has_permission || peer->channel_number == 0) {
        return TURN_ERR_PERMISSION;
    }

    uint8_t request[TURN_MAX_RESPONSE];
    if (data_len > TURN_MAX_RESPONSE - 4) {
        return PH_ERR_INVALID_ARG;
    }

    int req_len = turn_build_send_indication(request, sizeof(request),
                                              peer->channel_number,
                                              data, data_len);
    if (req_len < 0) {
        return PH_ERR_INVALID_ARG;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->servers[client->current_server].port);

    ssize_t sent = sendto(client->allocation.socket_fd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    return PH_OK;
}

int turn_recv_data(turn_client_t *client,
                   uint8_t *buffer,
                   size_t buffer_len,
                   char *peer_ip,
                   uint16_t *peer_port)
{
    if (!client || !buffer) {
        return PH_ERR_NULL_PTR;
    }

    if (client->allocation.state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    turn_refresh_allocation(client);

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(client->allocation.socket_fd, buffer,
                                 buffer_len, 0,
                                 (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return PH_ERR_NETWORK;
    }

    if (recv_len >= 4) {
        uint16_t channel_num = (buffer[0] << 8) | buffer[1];
        uint16_t data_len = (buffer[2] << 8) | buffer[3];

        if (turn_is_valid_channel(channel_num)) {

            if (peer_ip) {
                inet_ntop(AF_INET, &from_addr.sin_addr, peer_ip, 64);
            }
            if (peer_port) {
                *peer_port = ntohs(from_addr.sin_port);
            }

            return data_len;
        }
    }

    return (int)recv_len;
}
