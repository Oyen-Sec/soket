
#ifndef P2P_HOLEPUNCH_H
#define P2P_HOLEPUNCH_H

#include "phantom.h"
#include "stun_client.h"
#include "turn_client.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PH_P2P_MAX_CANDIDATES 6
#define PH_P2P_MAX_PEERS 3
#define PH_P2P_CHECK_INTERVAL_MS 500
#define PH_P2P_CHECK_TIMEOUT_MS 3000
#define PH_P2P_MAX_CHECK_RETRIES 6
#define PH_P2P_UDP_BUFFER_SIZE 2048
#define PH_P2P_PORT_RANGE_MIN 50000
#define PH_P2P_PORT_RANGE_MAX 51000

typedef enum {
    PH_P2P_CANDIDATE_HOST = 0,
    PH_P2P_CANDIDATE_SRFLX,
    PH_P2P_CANDIDATE_RELAY,
    PH_P2P_CANDIDATE_PRFLX
} ph_p2p_candidate_type_t;

#define PH_P2P_PRIORITY_HOST   2130706431
#define PH_P2P_PRIORITY_SRFLX  1694498815
#define PH_P2P_PRIORITY_RELAY  1258291199

typedef enum {
    PH_P2P_CHECK_PENDING = 0,
    PH_P2P_CHECK_IN_PROGRESS,
    PH_P2P_CHECK_SUCCEEDED,
    PH_P2P_CHECK_FAILED,
    PH_P2P_CHECK_FROZEN
} ph_p2p_check_state_t;

typedef enum {
    PH_P2P_STATE_IDLE = 0,
    PH_P2P_STATE_GATHERING,
    PH_P2P_STATE_CHECKING,
    PH_P2P_STATE_CONNECTED,
    PH_P2P_STATE_COMPLETED,
    PH_P2P_STATE_FAILED,
    PH_P2P_STATE_CLOSED
} ph_p2p_state_t;

typedef struct {
    char foundation[16];
    uint32_t priority;
    ph_p2p_candidate_type_t type;
    char transport[4];
    char address[128];
    uint16_t port;
    int socket_fd;
    int is_active;
    uint64_t created_at;
} ph_p2p_candidate_t;

typedef struct {
    ph_p2p_candidate_t *local;
    ph_p2p_candidate_t *remote;
    ph_p2p_check_state_t state;
    uint8_t transaction_id[12];
    uint64_t last_send_time;
    int retry_count;
    int is_nominated;
    uint64_t rtt_ms;
} ph_p2p_check_pair_t;

typedef struct {
    char peer_id[32];
    ph_p2p_candidate_t remote_candidates[PH_P2P_MAX_CANDIDATES];
    int remote_candidate_count;
    ph_p2p_check_pair_t check_pairs[PH_P2P_MAX_CANDIDATES * 2];
    int check_pair_count;
    ph_p2p_state_t state;
    int connected_fd;
    uint64_t connected_at;
    uint64_t last_activity;
} ph_p2p_peer_t;

typedef struct {
    ph_p2p_candidate_t local_candidates[PH_P2P_MAX_CANDIDATES];
    int local_candidate_count;
    ph_p2p_peer_t peers[PH_P2P_MAX_PEERS];
    int peer_count;
    ph_p2p_state_t state;

    ph_stun_client_t *stun_client;
    turn_client_t *turn_client;

    int udp_socket_fd;
    uint16_t local_port;

    uint64_t gathering_started;
    uint64_t checking_started;
    uint32_t check_interval_ms;

    int is_initialized;
    int use_ipv6;
    int enable_relay_fallback;
} ph_p2p_ctx_t;

int ph_p2p_init(ph_p2p_ctx_t *ctx, ph_stun_client_t *stun, turn_client_t *turn);
void ph_p2p_cleanup(ph_p2p_ctx_t *ctx);

int ph_p2p_gather_host_candidates(ph_p2p_ctx_t *ctx);
int ph_p2p_gather_srflx_candidates(ph_p2p_ctx_t *ctx, ph_stun_result_t *stun_result);
int ph_p2p_gather_relay_candidates(ph_p2p_ctx_t *ctx, turn_allocation_t *alloc);
int ph_p2p_gather_all_candidates(ph_p2p_ctx_t *ctx);

int ph_p2p_add_local_candidate(ph_p2p_ctx_t *ctx, ph_p2p_candidate_t *cand);
int ph_p2p_add_remote_candidate(ph_p2p_ctx_t *ctx, const char *peer_id,
                                 ph_p2p_candidate_t *cand);
const ph_p2p_candidate_t* ph_p2p_get_best_candidate(ph_p2p_ctx_t *ctx);
void ph_p2p_sort_candidates(ph_p2p_candidate_t *candidates, int count);

int ph_p2p_form_check_pairs(ph_p2p_ctx_t *ctx, const char *peer_id);
int ph_p2p_send_connectivity_check(ph_p2p_ctx_t *ctx, ph_p2p_check_pair_t *pair);
int ph_p2p_process_connectivity_check(ph_p2p_ctx_t *ctx, const uint8_t *data,
                                       size_t len, struct sockaddr_in *from);
int ph_p2p_run_connectivity_checks(ph_p2p_ctx_t *ctx, const char *peer_id);

int ph_p2p_add_peer(ph_p2p_ctx_t *ctx, const char *peer_id);
ph_p2p_peer_t* ph_p2p_find_peer(ph_p2p_ctx_t *ctx, const char *peer_id);
int ph_p2p_remove_peer(ph_p2p_ctx_t *ctx, const char *peer_id);

int ph_p2p_establish_connection(ph_p2p_ctx_t *ctx, const char *peer_id);
int ph_p2p_check_connection_ready(ph_p2p_ctx_t *ctx, const char *peer_id);
int ph_p2p_get_connected_socket(ph_p2p_ctx_t *ctx, const char *peer_id);

int ph_p2p_send_data(ph_p2p_ctx_t *ctx, const char *peer_id,
                     const uint8_t *data, size_t len);
int ph_p2p_recv_data(ph_p2p_ctx_t *ctx, const char *peer_id,
                     uint8_t *buffer, size_t max_len, size_t *recv_len);

int ph_p2p_udp_hole_punch(ph_p2p_ctx_t *ctx, const char *peer_id,
                          const char *remote_ip, uint16_t remote_port);
int ph_p2p_send_udp_probe(ph_p2p_ctx_t *ctx, const char *peer_id,
                          const char *ip, uint16_t port);

const char* ph_p2p_state_string(ph_p2p_state_t state);
const char* ph_p2p_candidate_type_string(ph_p2p_candidate_type_t type);
const char* ph_p2p_check_state_string(ph_p2p_check_state_t state);

uint32_t ph_p2p_calculate_priority(ph_p2p_candidate_type_t type, int preference);
int ph_p2p_generate_foundation(char *foundation, size_t len,
                                ph_p2p_candidate_type_t type, const char *address);
void ph_p2p_candidate_clear(ph_p2p_candidate_t *cand);
void ph_p2p_check_pair_clear(ph_p2p_check_pair_t *pair);

int ph_p2p_build_binding_request(uint8_t *buffer, size_t len,
                                  uint8_t *transaction_id);
int ph_p2p_parse_binding_response(const uint8_t *buffer, size_t len,
                                   char *mapped_ip, uint16_t *mapped_port);

#endif
