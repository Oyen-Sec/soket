
#ifndef DNS_RESOLVER_H
#define DNS_RESOLVER_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>

#define PH_DNS_MAX_NAMESERVERS 3
#define PH_DNS_MAX_SEARCH 6
#define PH_DNS_MAX_DOMAIN_LEN 256
#define PH_DNS_MAX_RESPONSE 512
#define PH_DNS_TXT_MAX_LEN 255

#define PH_DNS_TYPE_A     1
#define PH_DNS_TYPE_AAAA  28
#define PH_DNS_TYPE_TXT   16
#define PH_DNS_TYPE_MX    15
#define PH_DNS_TYPE_CNAME 5

typedef enum {
    PH_DNS_METHOD_GETADDRINFO = 0,
    PH_DNS_METHOD_RAW_UDP,
    PH_DNS_METHOD_DOH,
    PH_DNS_METHOD_UNKNOWN
} ph_dns_method_t;

typedef struct {
    char address[64];
    int is_loopback;
    int port;
} ph_nameserver_t;

typedef struct {
    ph_nameserver_t nameservers[PH_DNS_MAX_NAMESERVERS];
    int nameserver_count;
    char search_domains[PH_DNS_MAX_SEARCH][PH_DNS_MAX_DOMAIN_LEN];
    int search_count;
    char default_domain[PH_DNS_MAX_DOMAIN_LEN];
    int use_systemd_resolved;
    ph_dns_method_t preferred_method;
} ph_dns_config_t;

typedef struct {
    char hostname[PH_DNS_MAX_DOMAIN_LEN];
    char ip_address[64];
    int address_family;
    ph_dns_method_t method_used;
    int ttl;
    int success;
} ph_dns_result_t;

typedef struct {
    char domain[PH_DNS_MAX_DOMAIN_LEN];
    char txt_data[PH_DNS_TXT_MAX_LEN];
    int txt_length;
    int success;
} ph_dns_txt_record_t;

int ph_dns_config_init(ph_dns_config_t *config);
int ph_dns_config_parse_resolvconf(ph_dns_config_t *config, const char *path);
int ph_dns_config_detect_systemd_resolved(ph_dns_config_t *config);
const char* ph_dns_method_string(ph_dns_method_t method);

int ph_dns_resolve(ph_dns_result_t *result, const char *hostname,
                   const ph_dns_config_t *config);
int ph_dns_resolve_ipv4(char *ip_buffer, size_t buffer_len,
                        const char *hostname, const ph_dns_config_t *config);
int ph_dns_resolve_ipv6(char *ip_buffer, size_t buffer_len,
                        const char *hostname, const ph_dns_config_t *config);

int ph_dns_doh_resolve(ph_dns_result_t *result, const char *hostname,
                       const char *doh_server);

int ph_dns_raw_query(ph_dns_result_t *result, const char *hostname,
                     const char *nameserver, uint16_t type);

int ph_dns_query_txt(ph_dns_txt_record_t *record, const char *domain,
                     const ph_dns_config_t *config);
int ph_dns_txt_encode(char *encoded, size_t encoded_len,
                      const uint8_t *data, size_t data_len);
int ph_dns_txt_decode(uint8_t *decoded, size_t *decoded_len,
                      const char *encoded, size_t max_decoded_len);

int ph_dns_is_loopback(const char *address);
int ph_dns_validate_domain(const char *domain);
void ph_dns_result_clear(ph_dns_result_t *result);

#endif
