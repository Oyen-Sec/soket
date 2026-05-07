
#include "stun_client.h"
#include "dns_resolver.h"
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
#include <fcntl.h>

#define XOR_KEY 0xAB

static const uint8_t obf_stun1[] = {
    0x93, 0x8E, 0x9F, 0x8F, 0x8C, 0xA6, 0x8E, 0xA2, 0x8C, 0x8E, 0x9C, 0x8E, 0xA6, 0x8E, 0x8C
};

static const uint8_t obf_stun2[] = {
    0x93, 0x8E, 0x9F, 0x8F, 0x8C, 0xB2, 0xA6, 0x8E, 0xA2, 0x8C, 0x8E, 0x9C, 0x8E, 0xA6, 0x8E, 0x8C
};

static const uint8_t obf_stun3[] = {
    0x93, 0x8E, 0x9F, 0x8F, 0x8C, 0xB3, 0xA6, 0x8E, 0xA2, 0x8C, 0x8E, 0x9C, 0x8E, 0xA6, 0x8E, 0x8C
};

static const uint8_t obf_stun4[] = {
    0x93, 0x8E, 0x9F, 0x8F, 0xA6, 0x8C, 0x8E, 0x93, 0x8E, 0x8C, 0xA2, 0x8A, 0x9A, 0x8E, 0xA6, 0x8E, 0x8C
};

static void stun_decode_string(char *dest, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dest[i] = (char)(src[i] ^ XOR_KEY);
    }
    dest[len] = '\0';
}

int ph_stun_client_init(ph_stun_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    memset(client, 0, sizeof(ph_stun_client_t));
    client->server_count = 0;
    client->current_server = 0;
    client->timeout_ms = PH_STUN_TIMEOUT_MS;
    client->max_retries = PH_STUN_MAX_RETRIES;

    return PH_OK;
}

int ph_stun_client_add_server(ph_stun_client_t *client,
                               const char *address, uint16_t port)
{
    if (!client || !address) {
        return PH_ERR_NULL_PTR;
    }

    if (client->server_count >= PH_STUN_MAX_SERVERS) {
        return PH_ERR_INVALID_ARG;
    }

    strncpy(client->servers[client->server_count].address,
            address, sizeof(client->servers[0].address) - 1);
    client->servers[client->server_count].port = port;
    client->servers[client->server_count].is_primary = (client->server_count == 0);
    client->server_count++;

    return PH_OK;
}

int ph_stun_client_add_default_servers(ph_stun_client_t *client)
{
    if (!client) {
        return PH_ERR_NULL_PTR;
    }

    char server_name[64];

    stun_decode_string(server_name, obf_stun1, sizeof(obf_stun1));
    ph_stun_client_add_server(client, server_name, 19302);

    stun_decode_string(server_name, obf_stun2, sizeof(obf_stun2));
    ph_stun_client_add_server(client, server_name, 19302);

    stun_decode_string(server_name, obf_stun3, sizeof(obf_stun3));
    ph_stun_client_add_server(client, server_name, 19302);

    stun_decode_string(server_name, obf_stun4, sizeof(obf_stun4));
    ph_stun_client_add_server(client, server_name, 3478);

    return PH_OK;
}

void ph_stun_result_clear(ph_stun_result_t *result)
{
    if (!result) {
        return;
    }

    memset(result, 0, sizeof(ph_stun_result_t));
    result->success = 0;
    result->nat_type = PH_NAT_UNKNOWN;
    result->address_family = AF_INET;
}

void ph_stun_generate_transaction_id(uint8_t *transaction_id)
{
    if (!transaction_id) {
        return;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, transaction_id, PH_STUN_TRANSACTION_ID_SIZE);
        close(fd);
        if (n == PH_STUN_TRANSACTION_ID_SIZE) {
            return;
        }
    }

    uint64_t ts = ph_get_timestamp_ms();
    memcpy(transaction_id, &ts, sizeof(ts));
    memset(transaction_id + 8, 0, 4);
}

int ph_stun_build_binding_request(uint8_t *buffer, size_t buffer_len,
                                   uint8_t *transaction_id)
{
    if (!buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t *ptr = buffer;

    *ptr++ = 0x00;
    *ptr++ = 0x01;

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

    return (int)(ptr - buffer);
}

int ph_stun_parse_response(ph_stun_result_t *result, const uint8_t *buffer,
                           size_t buffer_len, uint8_t *transaction_id)
{
    if (!result || !buffer || buffer_len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    (void)transaction_id;

    const uint8_t *ptr = buffer;

    uint16_t msg_type = (ptr[0] << 8) | ptr[1];
    ptr += 2;

    if (msg_type != PH_STUN_BINDING_RESPONSE) {

        const char *err = "Bad msg type";
        size_t len = 12;
        if (len >= sizeof(result->error_message)) {
            len = sizeof(result->error_message) - 1;
        }
        memcpy(result->error_message, err, len);
        result->error_message[len] = '\0';
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

        if (attr_type == PH_STUN_ATTR_XOR_MAPPED_ADDRESS) {
            if (attr_len < 8) {
                continue;
            }

            uint8_t family = attr_ptr[1];
            uint16_t port = (attr_ptr[2] << 8) | attr_ptr[3];

            port ^= (PH_STUN_MAGIC_COOKIE >> 16) & 0xFFFF;

            if (family == 0x01) {

                if (attr_len < 8) {
                    continue;
                }

                uint32_t ip_addr;
                memcpy(&ip_addr, attr_ptr + 4, 4);
                ip_addr ^= PH_STUN_MAGIC_COOKIE;

                struct in_addr addr;
                addr.s_addr = ip_addr;
                inet_ntop(AF_INET, &addr, result->public_ip, sizeof(result->public_ip));
                result->public_port = port;
                result->address_family = AF_INET;
                result->success = 1;

                return PH_OK;
            }
        }

        attr_ptr += attr_len;
        if (attr_len % 4 != 0) {
            attr_ptr += 4 - (attr_len % 4);
        }
    }

    const char *err = "No XOR addr";
    size_t len = 11;
    if (len >= sizeof(result->error_message)) {
        len = sizeof(result->error_message) - 1;
    }
    memcpy(result->error_message, err, len);
    result->error_message[len] = '\0';
    return PH_ERR_INVALID_ARG;
}

int ph_stun_binding_request(ph_stun_result_t *result, ph_stun_client_t *client,
                            int server_index)
{
    if (!result || !client) {
        return PH_ERR_NULL_PTR;
    }

    if (server_index < 0 || server_index >= client->server_count) {
        return PH_ERR_INVALID_ARG;
    }

    ph_stun_result_clear(result);

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
        const char *err = "Socket err";
        size_t len = 10;
        if (len >= sizeof(result->error_message)) {
            len = sizeof(result->error_message) - 1;
        }
        memcpy(result->error_message, err, len);
        result->error_message[len] = '\0';
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

    uint8_t request[PH_STUN_MAX_RESPONSE];
    uint8_t transaction_id[PH_STUN_TRANSACTION_ID_SIZE];
    ph_stun_generate_transaction_id(transaction_id);

    int req_len = ph_stun_build_binding_request(request, sizeof(request),
                                                 transaction_id);
    if (req_len < 0) {
        const char *err = "Build req fail";
        size_t len = 14;
        if (len >= sizeof(result->error_message)) {
            len = sizeof(result->error_message) - 1;
        }
        memcpy(result->error_message, err, len);
        result->error_message[len] = '\0';
        close(sockfd);
        return PH_ERR_INVALID_ARG;
    }

    uint64_t start_time = ph_get_timestamp_ms();

    ssize_t sent = sendto(sockfd, request, req_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        const char *err = "Send fail";
        size_t len = 9;
        if (len >= sizeof(result->error_message)) {
            len = sizeof(result->error_message) - 1;
        }
        memcpy(result->error_message, err, len);
        result->error_message[len] = '\0';
        close(sockfd);
        return PH_ERR_NETWORK;
    }

    uint8_t response[PH_STUN_MAX_RESPONSE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t recv_len = recvfrom(sockfd, response, sizeof(response), 0,
                                (struct sockaddr *)&from_addr, &from_len);

    uint64_t end_time = ph_get_timestamp_ms();
    result->response_time_ms = (int)(end_time - start_time);

    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            const char *err = "Timeout";
            size_t len = 7;
            if (len >= sizeof(result->error_message)) {
                len = sizeof(result->error_message) - 1;
            }
            memcpy(result->error_message, err, len);
            result->error_message[len] = '\0';
        } else {
            const char *err = "Recv fail";
            size_t len = 9;
            if (len >= sizeof(result->error_message)) {
                len = sizeof(result->error_message) - 1;
            }
            memcpy(result->error_message, err, len);
            result->error_message[len] = '\0';
        }
        close(sockfd);
        return PH_ERR_TIMEOUT;
    }

    strncpy(result->server_used, client->servers[server_index].address,
            sizeof(result->server_used) - 1);

    ret = ph_stun_parse_response(result, response, recv_len, transaction_id);

    close(sockfd);

    if (ret != PH_OK) {
        return ret;
    }

    return PH_OK;
}

int ph_stun_discover(ph_stun_result_t *result, ph_stun_client_t *client)
{
    if (!result || !client) {
        return PH_ERR_NULL_PTR;
    }

    if (client->server_count == 0) {
        return PH_ERR_INVALID_ARG;
    }

    for (int i = 0; i < client->server_count; i++) {
        int ret = ph_stun_binding_request(result, client, i);
        if (ret == PH_OK && result->success) {

            result->nat_type = PH_NAT_FULL_CONE;
            return PH_OK;
        }
    }

    result->nat_type = PH_NAT_BLOCKED;
    return PH_ERR_NETWORK;
}

int ph_stun_detect_nat_type(ph_stun_result_t *result, ph_stun_client_t *client)
{
    if (!result || !client) {
        return PH_ERR_NULL_PTR;
    }

    int ret = ph_stun_discover(result, client);
    if (ret != PH_OK) {
        result->nat_type = PH_NAT_BLOCKED;
        return ret;
    }

    result->nat_type = PH_NAT_FULL_CONE;

    return PH_OK;
}

const char* ph_stun_nat_type_string(ph_nat_type_t nat_type)
{
    switch (nat_type) {
        case PH_NAT_OPEN_INTERNET:
            return "Open Internet";
        case PH_NAT_FULL_CONE:
            return "Full Cone";
        case PH_NAT_RESTRICTED:
            return "Restricted";
        case PH_NAT_PORT_RESTRICTED:
            return "Port Restricted";
        case PH_NAT_SYMMETRIC:
            return "Symmetric";
        case PH_NAT_BLOCKED:
            return "Blocked";
        default:
            return "Unknown";
    }
}

const char* ph_stun_nat_type_description(ph_nat_type_t nat_type)
{
    switch (nat_type) {
        case PH_NAT_OPEN_INTERNET:
            return "No NAT - Direct internet connection";
        case PH_NAT_FULL_CONE:
            return "Open NAT - Any external host can send packets";
        case PH_NAT_RESTRICTED:
            return "Restricted NAT - Only contacted hosts can send packets";
        case PH_NAT_PORT_RESTRICTED:
            return "Port Restricted NAT - Only contacted host:port can send";
        case PH_NAT_SYMMETRIC:
            return "Symmetric NAT - Different mapping per destination";
        case PH_NAT_BLOCKED:
            return "UDP Blocked - STUN failed completely";
        default:
            return "Unknown NAT type";
    }
}

uint32_t ph_stun_htonl(uint32_t value)
{
    return htonl(value);
}

uint16_t ph_stun_htons(uint16_t value)
{
    return htons(value);
}
