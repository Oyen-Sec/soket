
#include "p2p_holepunch.h"
#include "utils.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static uint64_t ph_p2p_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

int ph_p2p_init(ph_p2p_ctx_t *ctx, ph_stun_client_t *stun, turn_client_t *turn)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_p2p_ctx_t));

    ctx->stun_client = stun;
    ctx->turn_client = turn;

    ctx->state = PH_P2P_STATE_IDLE;
    ctx->local_candidate_count = 0;
    ctx->peer_count = 0;
    ctx->udp_socket_fd = -1;
    ctx->local_port = 0;
    ctx->check_interval_ms = PH_P2P_CHECK_INTERVAL_MS;
    ctx->is_initialized = 1;
    ctx->use_ipv6 = 0;
    ctx->enable_relay_fallback = 1;

    ctx->udp_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->udp_socket_fd < 0) {
        return PH_ERR_SOCKET;
    }

    int flags = fcntl(ctx->udp_socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(ctx->udp_socket_fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(ctx->udp_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ctx->udp_socket_fd);
        ctx->udp_socket_fd = -1;
        return PH_ERR_SOCKET;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(ctx->udp_socket_fd, (struct sockaddr*)&addr, &addr_len) == 0) {
        ctx->local_port = ntohs(addr.sin_port);
    }

    return PH_OK;
}

void ph_p2p_cleanup(ph_p2p_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->udp_socket_fd >= 0) {
        close(ctx->udp_socket_fd);
        ctx->udp_socket_fd = -1;
    }

    for (int i = 0; i < ctx->peer_count; i++) {
        if (ctx->peers[i].connected_fd >= 0) {
            close(ctx->peers[i].connected_fd);
            ctx->peers[i].connected_fd = -1;
        }
    }

    for (int i = 0; i < ctx->local_candidate_count; i++) {
        if (ctx->local_candidates[i].socket_fd >= 0) {
            close(ctx->local_candidates[i].socket_fd);
            ctx->local_candidates[i].socket_fd = -1;
        }
    }

    memset(ctx, 0, sizeof(ph_p2p_ctx_t));
    ctx->udp_socket_fd = -1;
}

int ph_p2p_generate_foundation(char *foundation, size_t len,
                                ph_p2p_candidate_type_t type, const char *address)
{
    if (!foundation || len < 16 || !address) {
        return PH_ERR_NULL_PTR;
    }

    char type_code;
    switch (type) {
        case PH_P2P_CANDIDATE_HOST:    type_code = 'h'; break;
        case PH_P2P_CANDIDATE_SRFLX:   type_code = 's'; break;
        case PH_P2P_CANDIDATE_RELAY:   type_code = 'r'; break;
        case PH_P2P_CANDIDATE_PRFLX:   type_code = 'p'; break;
        default:                       type_code = 'u'; break;
    }

    size_t addr_len = strlen(address);
    if (1 + 1 + addr_len >= len) {
        return PH_ERR_INVALID_ARG;
    }

    foundation[0] = type_code;
    foundation[1] = ':';
    memcpy(foundation + 2, address, addr_len);
    foundation[2 + addr_len] = '\0';

    return PH_OK;
}

uint32_t ph_p2p_calculate_priority(ph_p2p_candidate_type_t type, int preference)
{

    uint32_t base_priority;

    switch (type) {
        case PH_P2P_CANDIDATE_HOST:
            base_priority = PH_P2P_PRIORITY_HOST;
            break;
        case PH_P2P_CANDIDATE_SRFLX:
            base_priority = PH_P2P_PRIORITY_SRFLX;
            break;
        case PH_P2P_CANDIDATE_RELAY:
            base_priority = PH_P2P_PRIORITY_RELAY;
            break;
        case PH_P2P_CANDIDATE_PRFLX:
            base_priority = PH_P2P_PRIORITY_SRFLX - 1;
            break;
        default:
            base_priority = 0;
            break;
    }

    if (preference >= 0 && preference <= 127) {
        base_priority += (uint32_t)preference;
    }

    return base_priority;
}

void ph_p2p_candidate_clear(ph_p2p_candidate_t *cand)
{
    if (!cand) {
        return;
    }

    memset(cand, 0, sizeof(ph_p2p_candidate_t));
    cand->socket_fd = -1;
}

void ph_p2p_check_pair_clear(ph_p2p_check_pair_t *pair)
{
    if (!pair) {
        return;
    }

    memset(pair, 0, sizeof(ph_p2p_check_pair_t));
    pair->state = PH_P2P_CHECK_PENDING;
}

int ph_p2p_gather_host_candidates(ph_p2p_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    ctx->state = PH_P2P_STATE_GATHERING;

    ph_p2p_candidate_t cand;
    memset(&cand, 0, sizeof(cand));

    strncpy(cand.address, "0.0.0.0", sizeof(cand.address) - 1);
    cand.port = ctx->local_port;
    cand.type = PH_P2P_CANDIDATE_HOST;
    cand.socket_fd = ctx->udp_socket_fd;
    cand.is_active = 1;
    cand.created_at = ph_p2p_get_time_ms();
    cand.transport[0] = 'U'; cand.transport[1] = 'D'; cand.transport[2] = 'P';

    ph_p2p_generate_foundation(cand.foundation, sizeof(cand.foundation), cand.type, cand.address);
    cand.priority = ph_p2p_calculate_priority(cand.type, 127);

    return ph_p2p_add_local_candidate(ctx, &cand);
}

int ph_p2p_gather_srflx_candidates(ph_p2p_ctx_t *ctx, ph_stun_result_t *stun_result)
{
    if (!ctx || !stun_result || !stun_result->success) {
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->local_candidate_count >= PH_P2P_MAX_CANDIDATES) {
        return PH_ERR_MEMORY;
    }

    ph_p2p_candidate_t cand;
    memset(&cand, 0, sizeof(cand));

    strncpy(cand.address, stun_result->public_ip, sizeof(cand.address) - 1);
    cand.port = stun_result->public_port;
    cand.type = PH_P2P_CANDIDATE_SRFLX;
    cand.socket_fd = ctx->udp_socket_fd;
    cand.is_active = 1;
    cand.created_at = ph_p2p_get_time_ms();
    cand.transport[0] = 'U'; cand.transport[1] = 'D'; cand.transport[2] = 'P';

    ph_p2p_generate_foundation(cand.foundation, sizeof(cand.foundation), cand.type, cand.address);
    cand.priority = ph_p2p_calculate_priority(cand.type, 100);

    return ph_p2p_add_local_candidate(ctx, &cand);
}

int ph_p2p_gather_relay_candidates(ph_p2p_ctx_t *ctx, turn_allocation_t *alloc)
{
    if (!ctx || !alloc || alloc->state != TURN_ALLOC_STATE_ALLOCATED) {
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->local_candidate_count >= PH_P2P_MAX_CANDIDATES) {
        return PH_ERR_MEMORY;
    }

    ph_p2p_candidate_t cand;
    memset(&cand, 0, sizeof(cand));

    strncpy(cand.address, alloc->relayed_ip, sizeof(cand.address) - 1);
    cand.port = alloc->relayed_port;
    cand.type = PH_P2P_CANDIDATE_RELAY;
    cand.socket_fd = alloc->socket_fd;
    cand.is_active = 1;
    cand.created_at = ph_p2p_get_time_ms();
    cand.transport[0] = 'U'; cand.transport[1] = 'D'; cand.transport[2] = 'P';

    ph_p2p_generate_foundation(cand.foundation, sizeof(cand.foundation), cand.type, cand.address);
    cand.priority = ph_p2p_calculate_priority(cand.type, 50);

    return ph_p2p_add_local_candidate(ctx, &cand);
}

int ph_p2p_gather_all_candidates(ph_p2p_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    int ret = PH_OK;

    ret = ph_p2p_gather_host_candidates(ctx);
    if (ret != PH_OK) {
        return ret;
    }

    if (ctx->stun_client) {
        ph_stun_result_t stun_result;
        memset(&stun_result, 0, sizeof(stun_result));

        if (ph_stun_discover(&stun_result, ctx->stun_client) == PH_OK && stun_result.success) {
            ph_p2p_gather_srflx_candidates(ctx, &stun_result);
        }
    }

    if (ctx->enable_relay_fallback && ctx->turn_client) {
        turn_allocation_t *alloc = &ctx->turn_client->allocation;
        if (alloc->state == TURN_ALLOC_STATE_ALLOCATED) {
            ph_p2p_gather_relay_candidates(ctx, alloc);
        }
    }

    ph_p2p_sort_candidates(ctx->local_candidates, ctx->local_candidate_count);

    return PH_OK;
}

int ph_p2p_add_local_candidate(ph_p2p_ctx_t *ctx, ph_p2p_candidate_t *cand)
{
    if (!ctx || !cand) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->local_candidate_count >= PH_P2P_MAX_CANDIDATES) {
        return PH_ERR_MEMORY;
    }

    memcpy(&ctx->local_candidates[ctx->local_candidate_count], cand, sizeof(ph_p2p_candidate_t));
    ctx->local_candidate_count++;

    return PH_OK;
}

int ph_p2p_add_remote_candidate(ph_p2p_ctx_t *ctx, const char *peer_id,
                                 ph_p2p_candidate_t *cand)
{
    if (!ctx || !peer_id || !cand) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return PH_ERR_INVALID_ARG;
    }

    if (peer->remote_candidate_count >= PH_P2P_MAX_CANDIDATES) {
        return PH_ERR_MEMORY;
    }

    memcpy(&peer->remote_candidates[peer->remote_candidate_count], cand, sizeof(ph_p2p_candidate_t));
    peer->remote_candidate_count++;

    ph_p2p_sort_candidates(peer->remote_candidates, peer->remote_candidate_count);

    return PH_OK;
}

const ph_p2p_candidate_t* ph_p2p_get_best_candidate(ph_p2p_ctx_t *ctx)
{
    if (!ctx || ctx->local_candidate_count == 0) {
        return NULL;
    }

    for (int i = 0; i < ctx->local_candidate_count; i++) {
        if (ctx->local_candidates[i].is_active) {
            return &ctx->local_candidates[i];
        }
    }

    return NULL;
}

void ph_p2p_sort_candidates(ph_p2p_candidate_t *candidates, int count)
{
    if (!candidates || count <= 1) {
        return;
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (candidates[j].priority < candidates[j + 1].priority) {

                ph_p2p_candidate_t temp = candidates[j];
                candidates[j] = candidates[j + 1];
                candidates[j + 1] = temp;
            }
        }
    }
}

int ph_p2p_add_peer(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->peer_count >= PH_P2P_MAX_PEERS) {
        return PH_ERR_MEMORY;
    }

    if (ph_p2p_find_peer(ctx, peer_id)) {
        return PH_OK;
    }

    ph_p2p_peer_t *peer = &ctx->peers[ctx->peer_count];
    memset(peer, 0, sizeof(ph_p2p_peer_t));
    strncpy(peer->peer_id, peer_id, sizeof(peer->peer_id) - 1);
    peer->state = PH_P2P_STATE_IDLE;
    peer->connected_fd = -1;
    peer->remote_candidate_count = 0;
    peer->check_pair_count = 0;

    ctx->peer_count++;
    return PH_OK;
}

ph_p2p_peer_t* ph_p2p_find_peer(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return NULL;
    }

    for (int i = 0; i < ctx->peer_count; i++) {
        if (strcmp(ctx->peers[i].peer_id, peer_id) == 0) {
            return &ctx->peers[i];
        }
    }

    return NULL;
}

int ph_p2p_remove_peer(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return PH_ERR_NULL_PTR;
    }

    for (int i = 0; i < ctx->peer_count; i++) {
        if (strcmp(ctx->peers[i].peer_id, peer_id) == 0) {

            if (ctx->peers[i].connected_fd >= 0) {
                close(ctx->peers[i].connected_fd);
            }

            for (int j = i; j < ctx->peer_count - 1; j++) {
                ctx->peers[j] = ctx->peers[j + 1];
            }

            ctx->peer_count--;
            return PH_OK;
        }
    }

    return PH_ERR_INVALID_ARG;
}

int ph_p2p_form_check_pairs(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return PH_ERR_INVALID_ARG;
    }

    peer->check_pair_count = 0;

    int max_local = (ctx->local_candidate_count < 3) ? ctx->local_candidate_count : 3;
    int max_remote = (peer->remote_candidate_count < 3) ? peer->remote_candidate_count : 3;

    for (int i = 0; i < max_local; i++) {
        for (int j = 0; j < max_remote; j++) {
            if (peer->check_pair_count >= PH_P2P_MAX_CANDIDATES * 2) {
                break;
            }

            ph_p2p_check_pair_t *pair = &peer->check_pairs[peer->check_pair_count];
            ph_p2p_check_pair_clear(pair);

            pair->local = &ctx->local_candidates[i];
            pair->remote = &peer->remote_candidates[j];
            pair->state = PH_P2P_CHECK_PENDING;
            pair->retry_count = 0;
            pair->is_nominated = 0;

            peer->check_pair_count++;
        }
    }

    return PH_OK;
}

int ph_p2p_build_binding_request(uint8_t *buffer, size_t len,
                                  uint8_t *transaction_id)
{
    if (!buffer || len < 20 || !transaction_id) {
        return PH_ERR_NULL_PTR;
    }

    memset(buffer, 0, len);

    uint16_t *msg_type = (uint16_t*)buffer;
    *msg_type = htons(PH_STUN_BINDING_REQUEST);

    uint16_t *msg_len = (uint16_t*)(buffer + 2);
    *msg_len = 0;

    uint32_t *magic = (uint32_t*)(buffer + 4);
    *magic = htonl(PH_STUN_MAGIC_COOKIE);

    memcpy(buffer + 8, transaction_id, 12);

    return 20;
}

int ph_p2p_parse_binding_response(const uint8_t *buffer, size_t len,
                                   char *mapped_ip, uint16_t *mapped_port)
{
    if (!buffer || len < 20 || !mapped_ip || !mapped_port) {
        return PH_ERR_NULL_PTR;
    }

    uint16_t msg_type = ntohs(*(uint16_t*)buffer);
    if (msg_type != PH_STUN_BINDING_RESPONSE) {
        return PH_ERR_INVALID_ARG;
    }

    size_t offset = 20;
    uint16_t attr_len = ntohs(*(uint16_t*)(buffer + 2));

    while (offset + 4 <= len) {
        uint16_t attr_type = ntohs(*(uint16_t*)(buffer + offset));
        attr_len = ntohs(*(uint16_t*)(buffer + offset + 2));

        if (offset + 4 + attr_len > len) {
            break;
        }

        if (attr_type == PH_STUN_ATTR_XOR_MAPPED_ADDRESS) {
            const uint8_t *attr_data = buffer + offset + 4;

            uint8_t family = attr_data[1];
            uint16_t xport = *(uint16_t*)(attr_data + 2);

            if (family == 0x01) {
                uint32_t xaddr = *(uint32_t*)(attr_data + 4);

                *mapped_port = ntohs(xport ^ (PH_STUN_MAGIC_COOKIE >> 16));
                uint32_t addr = ntohl(xaddr ^ PH_STUN_MAGIC_COOKIE);

                inet_ntop(AF_INET, &addr, mapped_ip, 64);
                return PH_OK;
            }
        }

        offset += 4 + attr_len;
    }

    return PH_ERR_INVALID_ARG;
}

int ph_p2p_send_connectivity_check(ph_p2p_ctx_t *ctx, ph_p2p_check_pair_t *pair)
{
    if (!ctx || !pair || !pair->local || !pair->remote) {
        return PH_ERR_NULL_PTR;
    }

    for (int i = 0; i < 12; i++) {
        pair->transaction_id[i] = (uint8_t)(rand() % 256);
    }

    uint8_t buffer[PH_P2P_UDP_BUFFER_SIZE];
    int msg_len = ph_p2p_build_binding_request(buffer, sizeof(buffer), pair->transaction_id);
    if (msg_len < 0) {
        return msg_len;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(pair->remote->port);

    if (inet_pton(AF_INET, pair->remote->address, &dest.sin_addr) != 1) {
        return PH_ERR_INVALID_ARG;
    }

    ssize_t sent = sendto(ctx->udp_socket_fd, buffer, msg_len, 0,
                          (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    pair->state = PH_P2P_CHECK_IN_PROGRESS;
    pair->last_send_time = ph_p2p_get_time_ms();
    pair->retry_count++;

    return PH_OK;
}

int ph_p2p_process_connectivity_check(ph_p2p_ctx_t *ctx, const uint8_t *data,
                                       size_t len, struct sockaddr_in *from)
{
    if (!ctx || !data || !from) {
        return PH_ERR_NULL_PTR;
    }

    if (len < 20) {
        return PH_ERR_INVALID_ARG;
    }

    uint16_t msg_type = ntohs(*(uint16_t*)data);

    if (msg_type == PH_STUN_BINDING_REQUEST) {

        uint8_t transaction_id[12];
        memcpy(transaction_id, data + 8, 12);

        uint8_t response[PH_P2P_UDP_BUFFER_SIZE];
        memset(response, 0, sizeof(response));

        uint16_t *resp_type = (uint16_t*)response;
        *resp_type = htons(PH_STUN_BINDING_RESPONSE);

        uint32_t *magic = (uint32_t*)(response + 4);
        *magic = htonl(PH_STUN_MAGIC_COOKIE);

        memcpy(response + 8, transaction_id, 12);

        uint8_t *attr = response + 20;
        uint16_t *attr_type = (uint16_t*)attr;
        *attr_type = htons(PH_STUN_ATTR_XOR_MAPPED_ADDRESS);

        uint16_t *attr_len = (uint16_t*)(attr + 2);
        *attr_len = htons(8);

        attr[4] = 0;
        attr[5] = 0x01;

        uint16_t xport = htons(from->sin_port) ^ (PH_STUN_MAGIC_COOKIE >> 16);
        *(uint16_t*)(attr + 6) = xport;

        uint32_t xaddr = htonl(from->sin_addr.s_addr) ^ PH_STUN_MAGIC_COOKIE;
        *(uint32_t*)(attr + 8) = xaddr;

        ssize_t sent = sendto(ctx->udp_socket_fd, response, 28, 0,
                              (struct sockaddr*)from, sizeof(*from));
        if (sent < 0) {
            return PH_ERR_NETWORK;
        }

        return PH_OK;
    } else if (msg_type == PH_STUN_BINDING_RESPONSE) {

        const uint8_t *trans_id = data + 8;

        for (int i = 0; i < ctx->peer_count; i++) {
            for (int j = 0; j < ctx->peers[i].check_pair_count; j++) {
                ph_p2p_check_pair_t *pair = &ctx->peers[i].check_pairs[j];

                if (memcmp(pair->transaction_id, trans_id, 12) == 0) {

                    pair->state = PH_P2P_CHECK_SUCCEEDED;
                    pair->rtt_ms = ph_p2p_get_time_ms() - pair->last_send_time;

                    pair->is_nominated = 1;

                    return PH_OK;
                }
            }
        }
    }

    return PH_ERR_INVALID_ARG;
}

int ph_p2p_run_connectivity_checks(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return PH_ERR_INVALID_ARG;
    }

    ctx->state = PH_P2P_STATE_CHECKING;
    ctx->checking_started = ph_p2p_get_time_ms();

    int checks_sent = 0;

    for (int i = 0; i < peer->check_pair_count; i++) {
        ph_p2p_check_pair_t *pair = &peer->check_pairs[i];

        if (pair->state == PH_P2P_CHECK_PENDING || pair->state == PH_P2P_CHECK_FROZEN) {
            if (ph_p2p_send_connectivity_check(ctx, pair) == PH_OK) {
                checks_sent++;
            }
        }
    }

    return checks_sent;
}

int ph_p2p_udp_hole_punch(ph_p2p_ctx_t *ctx, const char *peer_id,
                          const char *remote_ip, uint16_t remote_port)
{
    if (!ctx || !peer_id || !remote_ip) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_candidate_t remote_cand;
    memset(&remote_cand, 0, sizeof(remote_cand));
    strncpy(remote_cand.address, remote_ip, sizeof(remote_cand.address) - 1);
    remote_cand.port = remote_port;
    remote_cand.type = PH_P2P_CANDIDATE_HOST;
    remote_cand.is_active = 1;
    remote_cand.transport[0] = 'U'; remote_cand.transport[1] = 'D'; remote_cand.transport[2] = 'P';

    ph_p2p_add_remote_candidate(ctx, peer_id, &remote_cand);

    ph_p2p_form_check_pairs(ctx, peer_id);

    return ph_p2p_run_connectivity_checks(ctx, peer_id);
}

int ph_p2p_send_udp_probe(ph_p2p_ctx_t *ctx, const char *peer_id,
                          const char *ip, uint16_t port)
{
    if (!ctx || !ip) {
        return PH_ERR_NULL_PTR;
    }

    (void)peer_id;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) {
        return PH_ERR_INVALID_ARG;
    }

    uint8_t probe[32];
    memset(probe, 0, sizeof(probe));
    probe[0] = 0x01;

    ssize_t sent = sendto(ctx->udp_socket_fd, probe, sizeof(probe), 0,
                          (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    return PH_OK;
}

int ph_p2p_check_connection_ready(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return 0;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return 0;
    }

    for (int i = 0; i < peer->check_pair_count; i++) {
        if (peer->check_pairs[i].state == PH_P2P_CHECK_SUCCEEDED &&
            peer->check_pairs[i].is_nominated) {
            return 1;
        }
    }

    return 0;
}

int ph_p2p_get_connected_socket(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return -1;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return -1;
    }

    return peer->connected_fd;
}

int ph_p2p_establish_connection(ph_p2p_ctx_t *ctx, const char *peer_id)
{
    if (!ctx || !peer_id) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer) {
        return PH_ERR_INVALID_ARG;
    }

    for (int i = 0; i < peer->check_pair_count; i++) {
        ph_p2p_check_pair_t *pair = &peer->check_pairs[i];

        if (pair->state == PH_P2P_CHECK_SUCCEEDED && pair->is_nominated) {

            peer->state = PH_P2P_STATE_CONNECTED;
            peer->connected_fd = ctx->udp_socket_fd;
            peer->connected_at = ph_p2p_get_time_ms();
            peer->last_activity = peer->connected_at;

            ctx->state = PH_P2P_STATE_COMPLETED;

            return PH_OK;
        }
    }

    return PH_ERR_TIMEOUT;
}

int ph_p2p_send_data(ph_p2p_ctx_t *ctx, const char *peer_id,
                     const uint8_t *data, size_t len)
{
    if (!ctx || !peer_id || !data) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer || peer->state != PH_P2P_STATE_CONNECTED) {
        return PH_ERR_NETWORK;
    }

    for (int i = 0; i < peer->remote_candidate_count; i++) {
        if (!peer->remote_candidates[i].is_active) {
            continue;
        }

        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(peer->remote_candidates[i].port);

        if (inet_pton(AF_INET, peer->remote_candidates[i].address, &dest.sin_addr) != 1) {
            continue;
        }

        ssize_t sent = sendto(ctx->udp_socket_fd, data, len, 0,
                              (struct sockaddr*)&dest, sizeof(dest));
        if (sent >= 0) {
            peer->last_activity = ph_p2p_get_time_ms();
            return (int)sent;
        }
    }

    return PH_ERR_NETWORK;
}

int ph_p2p_recv_data(ph_p2p_ctx_t *ctx, const char *peer_id,
                     uint8_t *buffer, size_t max_len, size_t *recv_len)
{
    if (!ctx || !peer_id || !buffer) {
        return PH_ERR_NULL_PTR;
    }

    ph_p2p_peer_t *peer = ph_p2p_find_peer(ctx, peer_id);
    if (!peer || peer->state != PH_P2P_STATE_CONNECTED) {
        return PH_ERR_NETWORK;
    }

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    ssize_t received = recvfrom(ctx->udp_socket_fd, buffer, max_len, 0,
                                (struct sockaddr*)&from, &from_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return PH_ERR_TIMEOUT;
        }
        return PH_ERR_NETWORK;
    }

    if (recv_len) {
        *recv_len = (size_t)received;
    }

    peer->last_activity = ph_p2p_get_time_ms();

    return PH_OK;
}

static const char* ph_p2p_get_state_char(ph_p2p_state_t state)
{
    switch (state) {
        case PH_P2P_STATE_IDLE:       return "I";
        case PH_P2P_STATE_GATHERING:  return "G";
        case PH_P2P_STATE_CHECKING:   return "C";
        case PH_P2P_STATE_CONNECTED:  return "N";
        case PH_P2P_STATE_COMPLETED:  return "D";
        case PH_P2P_STATE_FAILED:     return "F";
        case PH_P2P_STATE_CLOSED:     return "X";
        default:                      return "?";
    }
}

const char* ph_p2p_state_string(ph_p2p_state_t state) {
    return ph_p2p_get_state_char(state);
}

const char* ph_p2p_candidate_type_string(ph_p2p_candidate_type_t type) {
    switch (type) {
        case PH_P2P_CANDIDATE_HOST:   return "h";
        case PH_P2P_CANDIDATE_SRFLX:  return "s";
        case PH_P2P_CANDIDATE_RELAY:  return "r";
        case PH_P2P_CANDIDATE_PRFLX:  return "p";
        default:                      return "u";
    }
}

const char* ph_p2p_check_state_string(ph_p2p_check_state_t state) {
    switch (state) {
        case PH_P2P_CHECK_PENDING:     return "P";
        case PH_P2P_CHECK_IN_PROGRESS: return "I";
        case PH_P2P_CHECK_SUCCEEDED:   return "S";
        case PH_P2P_CHECK_FAILED:      return "F";
        case PH_P2P_CHECK_FROZEN:      return "Z";
        default:                       return "?";
    }
}
