
#ifndef TURN_CLIENT_H
#define TURN_CLIENT_H

#include "phantom.h"
#include "stun_client.h"
#include <stdint.h>
#include <stddef.h>

#define TURN_PORT 3478
#define TURN_MAX_SERVERS 5
#define TURN_ALLOCATE_LIFETIME 600
#define TURN_REFRESH_INTERVAL 10
#define TURN_CHANNEL_MIN 0x4000
#define TURN_CHANNEL_MAX 0x7FFF
#define TURN_MAX_RESPONSE 1024
#define TURN_TIMEOUT_MS 5000
#define TURN_MAX_RETRIES 3

#define TURN_ALLOCATE           0x0003
#define TURN_ALLOCATE_SUCCESS   0x0103
#define TURN_REFRESH            0x0004
#define TURN_REFRESH_SUCCESS    0x0104
#define TURN_SEND               0x0004
#define TURN_DATA               0x0006
#define TURN_CREATE_PERMISSION  0x0008
#define TURN_CREATE_PERMISSION_SUCCESS 0x0108
#define TURN_CHANNEL_BIND       0x0009
#define TURN_CHANNEL_BIND_SUCCESS 0x0109
#define TURN_SEND_INDICATION    0x0011

#define TURN_ATTR_LIFETIME          0x000D
#define TURN_ATTR_XOR_RELAYED_ADDRESS 0x0016
#define TURN_ATTR_REQUESTED_TRANSPORT 0x0019
#define TURN_ATTR_DONT_FRAGMENT     0x001A
#define TURN_ATTR_XOR_PEER_ADDRESS  0x0012
#define TURN_ATTR_CHANNEL_NUMBER    0x000C
#define TURN_ATTR_NONCE             0x0015
#define TURN_ATTR_REALM             0x0014
#define TURN_ATTR_USERNAME          0x0006
#define TURN_ATTR_MESSAGE_INTEGRITY 0x0008
#define TURN_ATTR_ERROR_CODE        0x0009
#define TURN_ATTR_SOFTWARE          0x8022

#define TURN_TRANSPORT_UDP 17
#define TURN_TRANSPORT_TCP 6

#define TURN_CREDENTIAL_SIZE 128
#define TURN_NONCE_SIZE 128
#define TURN_REALM_SIZE 128
#define TURN_USERNAME_SIZE 128
#define TURN_HMAC_SIZE 20

#define TURN_ERR_ALLOCATE -20
#define TURN_ERR_REFRESH -21
#define TURN_ERR_PERMISSION -22
#define TURN_ERR_CHANNEL -23
#define TURN_ERR_AUTH -24
#define TURN_ERR_TIMEOUT -25
#define TURN_ERR_NETWORK -26

typedef enum {
    TURN_ALLOC_STATE_IDLE = 0,
    TURN_ALLOC_STATE_ALLOCATING,
    TURN_ALLOC_STATE_ALLOCATED,
    TURN_ALLOC_STATE_REFRESHING,
    TURN_ALLOC_STATE_EXPIRED,
    TURN_ALLOC_STATE_ERROR
} turn_alloc_state_t;

typedef struct {
    char address[256];
    uint16_t port;
    int is_primary;
    uint8_t transport;
} turn_server_t;

typedef struct {
    char username[TURN_USERNAME_SIZE];
    char password[TURN_CREDENTIAL_SIZE];
    char realm[TURN_REALM_SIZE];
    char nonce[TURN_NONCE_SIZE];
    int has_credentials;
} turn_credentials_t;

typedef struct {
    turn_alloc_state_t state;
    char relayed_ip[64];
    uint16_t relayed_port;
    uint32_t lifetime;
    uint32_t time_remaining;
    uint16_t channel_number;
    int socket_fd;
    uint64_t last_refresh;
    int retry_count;
} turn_allocation_t;

typedef struct {
    turn_server_t servers[TURN_MAX_SERVERS];
    int server_count;
    int current_server;
    turn_credentials_t credentials;
    turn_allocation_t allocation;
    uint32_t timeout_ms;
    int max_retries;
    uint8_t transport;
    int is_initialized;
} turn_client_t;

typedef struct {
    char peer_ip[64];
    uint16_t peer_port;
    int has_permission;
    uint16_t channel_number;
} turn_peer_t;

int turn_client_init(turn_client_t *client);
int turn_client_set_credentials(turn_client_t *client,
                                 const char *username,
                                 const char *password);
int turn_client_add_server(turn_client_t *client,
                           const char *address,
                           uint16_t port,
                           uint8_t transport);
int turn_client_add_default_servers(turn_client_t *client);

int turn_allocate(turn_client_t *client);
int turn_allocate_request(turn_allocation_t *alloc,
                          turn_client_t *client,
                          int server_index);
int turn_refresh_allocation(turn_client_t *client);
int turn_free_allocation(turn_client_t *client);

int turn_create_permission(turn_client_t *client,
                           const char *peer_ip,
                           uint16_t peer_port);
int turn_bind_channel(turn_client_t *client,
                      turn_peer_t *peer);

int turn_send_data(turn_client_t *client,
                   turn_peer_t *peer,
                   const uint8_t *data,
                   size_t data_len);
int turn_recv_data(turn_client_t *client,
                   uint8_t *buffer,
                   size_t buffer_len,
                   char *peer_ip,
                   uint16_t *peer_port);

int turn_build_allocate_request(uint8_t *buffer,
                                 size_t buffer_len,
                                 uint8_t *transaction_id,
                                 uint32_t lifetime);
int turn_build_refresh_request(uint8_t *buffer,
                                size_t buffer_len,
                                uint8_t *transaction_id,
                                uint32_t lifetime);
int turn_build_permission_request(uint8_t *buffer,
                                   size_t buffer_len,
                                   uint8_t *transaction_id,
                                   const char *peer_ip);
int turn_build_channel_bind_request(uint8_t *buffer,
                                     size_t buffer_len,
                                     uint8_t *transaction_id,
                                     uint16_t channel_number,
                                     const char *peer_ip);
int turn_build_send_indication(uint8_t *buffer,
                                size_t buffer_len,
                                uint16_t channel_number,
                                const uint8_t *data,
                                size_t data_len);

int turn_parse_allocate_response(turn_allocation_t *alloc,
                                  const uint8_t *buffer,
                                  size_t buffer_len);
int turn_parse_refresh_response(turn_allocation_t *alloc,
                                 const uint8_t *buffer,
                                 size_t buffer_len);
int turn_parse_error_response(const uint8_t *buffer,
                               size_t buffer_len,
                               char *error_msg,
                               size_t error_len);

int turn_compute_message_integrity(uint8_t *hmac,
                                    const uint8_t *message,
                                    size_t message_len,
                                    const char *password);
int turn_add_authentication(uint8_t *buffer,
                             size_t buffer_len,
                             int msg_len,
                             const turn_credentials_t *creds);

void turn_allocation_clear(turn_allocation_t *alloc);
void turn_client_cleanup(turn_client_t *client);
const char* turn_state_string(turn_alloc_state_t state);
uint16_t turn_allocate_channel_number(void);
int turn_is_valid_channel(uint16_t channel);

#endif
