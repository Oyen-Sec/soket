
#ifndef STUN_CLIENT_H
#define STUN_CLIENT_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>

#define PH_STUN_MAGIC_COOKIE 0x2112A442
#define PH_STUN_PORT 3478
#define PH_STUN_MAX_SERVERS 5
#define PH_STUN_TRANSACTION_ID_SIZE 12
#define PH_STUN_MAX_RESPONSE 1024
#define PH_STUN_TIMEOUT_MS 3000
#define PH_STUN_MAX_RETRIES 3

#define PH_STUN_BINDING_REQUEST  0x0001
#define PH_STUN_BINDING_RESPONSE 0x0101
#define PH_STUN_BINDING_ERROR    0x0111

#define PH_STUN_ATTR_MAPPED_ADDRESS     0x0001
#define PH_STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020
#define PH_STUN_ATTR_CHANGE_REQUEST     0x0003
#define PH_STUN_ATTR_PRIORITY           0x0024
#define PH_STUN_ATTR_ERROR_CODE         0x0009
#define PH_STUN_ATTR_UNKNOWN_ATTRIBUTES 0x000A
#define PH_STUN_ATTR_SOFTWARE           0x8022

typedef enum {
    PH_NAT_UNKNOWN = 0,
    PH_NAT_OPEN_INTERNET,
    PH_NAT_FULL_CONE,
    PH_NAT_RESTRICTED,
    PH_NAT_PORT_RESTRICTED,
    PH_NAT_SYMMETRIC,
    PH_NAT_BLOCKED
} ph_nat_type_t;

typedef struct {
    char address[256];
    uint16_t port;
    int is_primary;
} ph_stun_server_t;

typedef struct {
    char public_ip[64];
    uint16_t public_port;
    int address_family;
    ph_nat_type_t nat_type;
    int success;
    char error_message[256];
    int response_time_ms;
    char server_used[256];
} ph_stun_result_t;

typedef struct {
    ph_stun_server_t servers[PH_STUN_MAX_SERVERS];
    int server_count;
    int current_server;
    uint32_t timeout_ms;
    int max_retries;
} ph_stun_client_t;

int ph_stun_client_init(ph_stun_client_t *client);
int ph_stun_client_add_server(ph_stun_client_t *client,
                               const char *address, uint16_t port);
int ph_stun_client_add_default_servers(ph_stun_client_t *client);

int ph_stun_discover(ph_stun_result_t *result, ph_stun_client_t *client);
int ph_stun_binding_request(ph_stun_result_t *result, ph_stun_client_t *client,
                            int server_index);

int ph_stun_detect_nat_type(ph_stun_result_t *result, ph_stun_client_t *client);
const char* ph_stun_nat_type_string(ph_nat_type_t nat_type);
const char* ph_stun_nat_type_description(ph_nat_type_t nat_type);

int ph_stun_build_binding_request(uint8_t *buffer, size_t buffer_len,
                                   uint8_t *transaction_id);
int ph_stun_parse_response(ph_stun_result_t *result, const uint8_t *buffer,
                           size_t buffer_len, uint8_t *transaction_id);

void ph_stun_result_clear(ph_stun_result_t *result);
void ph_stun_generate_transaction_id(uint8_t *transaction_id);
uint32_t ph_stun_htonl(uint32_t value);
uint16_t ph_stun_htons(uint16_t value);

#endif
