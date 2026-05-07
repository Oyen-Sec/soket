
#ifndef NETWORK_CORE_H
#define NETWORK_CORE_H

#include "phantom.h"
#include "crypto_engine.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct {
    int socket_fd;
    int is_blocking;
    uint32_t connect_timeout_ms;
    uint32_t read_timeout_ms;
    uint32_t write_timeout_ms;
    int is_connected;
    ph_relay_t relay;
    uint64_t last_activity;
    uint64_t last_keepalive;
} ph_connection_t;

typedef struct {
    uint32_t current_delay_ms;
    uint32_t max_delay_ms;
    uint32_t min_delay_ms;
    uint32_t retry_count;
    uint32_t max_retries;
    uint64_t last_attempt;
    int is_backing_off;
} ph_reconnect_state_t;

typedef struct {
    uint8_t encrypted_ticket[256];
    size_t ticket_len;
    uint64_t created_at;
    uint64_t expires_at;
    int is_valid;
} ph_session_ticket_t;

typedef struct {
    ph_relay_t relays[PH_MAX_RELAYS];
    int relay_count;
    int current_index;
    int active_count;
    uint32_t switch_threshold_ms;
    uint64_t last_switch;
} ph_relay_manager_t;

typedef struct {
    uint32_t interval_ms;
    uint32_t timeout_ms;
    uint64_t last_sent;
    uint64_t last_received;
    int pending_pong;
    uint8_t ping_key[PH_CRYPTO_KEY_SIZE];
} ph_keepalive_t;

typedef struct {
    ph_connection_t connection;
    ph_reconnect_state_t reconnect;
    ph_relay_manager_t relay_mgr;
    ph_session_ticket_t session_ticket;
    ph_keepalive_t keepalive;
    uint32_t jitter_min_ms;
    uint32_t jitter_max_ms;
    int is_initialized;
} ph_network_ctx_t;

int ph_network_init(ph_network_ctx_t *ctx);
void ph_network_cleanup(ph_network_ctx_t *ctx);

int ph_socket_create(int domain, int type, int protocol);
int ph_socket_set_nonblocking(int fd);
int ph_socket_set_blocking(int fd);
int ph_socket_connect(int fd, const char *address, uint16_t port, uint32_t timeout_ms);
int ph_socket_send(int fd, const void *data, size_t len, uint32_t timeout_ms);
int ph_socket_recv(int fd, void *buffer, size_t len, uint32_t timeout_ms);
void ph_socket_close(int fd);

int ph_reconnect_init(ph_reconnect_state_t *state);
int ph_reconnect_attempt(ph_reconnect_state_t *state);
void ph_reconnect_reset(ph_reconnect_state_t *state);
uint32_t ph_reconnect_get_delay(ph_reconnect_state_t *state);

int ph_relay_manager_init(ph_relay_manager_t *mgr);
int ph_relay_manager_add(ph_relay_manager_t *mgr, const char *address,
                         uint16_t port, int priority);
int ph_relay_manager_switch(ph_relay_manager_t *mgr);
ph_relay_t* ph_relay_manager_get_current(ph_relay_manager_t *mgr);
int ph_relay_manager_should_switch(ph_relay_manager_t *mgr, uint32_t elapsed_ms);

int ph_session_ticket_create(ph_session_ticket_t *ticket,
                              const uint8_t *session_key,
                              size_t key_len);
int ph_session_ticket_validate(const ph_session_ticket_t *ticket);
int ph_session_ticket_decrypt(const ph_session_ticket_t *ticket,
                               uint8_t *session_key,
                               size_t key_len,
                               const uint8_t *decrypt_key);

int ph_keepalive_init(ph_keepalive_t *ka, const uint8_t *key);
int ph_keepalive_send(int fd, ph_keepalive_t *ka);
int ph_keepalive_recv(int fd, ph_keepalive_t *ka);
int ph_keepalive_check(ph_keepalive_t *ka, uint64_t now_ms);

int ph_network_apply_jitter(uint32_t min_ms, uint32_t max_ms);
int ph_network_randomize_packet_size(void *buffer, size_t *len, size_t max_len);

int ph_network_connect(ph_network_ctx_t *ctx, const char *address, uint16_t port);
int ph_network_disconnect(ph_network_ctx_t *ctx);
int ph_network_reconnect(ph_network_ctx_t *ctx);
int ph_network_send_encrypted(ph_network_ctx_t *ctx, const uint8_t *data,
                               size_t len, const uint8_t *key);
int ph_network_recv_encrypted(ph_network_ctx_t *ctx, uint8_t *buffer,
                               size_t max_len, size_t *recv_len,
                               const uint8_t *key);
int ph_network_is_connected(ph_network_ctx_t *ctx);
int ph_network_send_keepalive(ph_network_ctx_t *ctx);

#endif
