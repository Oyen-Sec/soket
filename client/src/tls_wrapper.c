#include "tls_wrapper.h"
#include "utils.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
	int fd = *(int *)ctx;
	for (;;) {
		ssize_t rlen = read(fd, buf, len);
		if (rlen < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (rlen == 0) return -1;
		return (int)rlen;
	}
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
	int fd = *(int *)ctx;
	for (;;) {
		ssize_t wlen = write(fd, buf, len);
		if (wlen < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		return (int)wlen;
	}
}

static int is_ip_address(const char *host) {
    if (!host) return 0;
    for (int i = 0; host[i]; i++) {
        if (!((host[i] >= '0' && host[i] <= '9') || host[i] == '.' || host[i] == ':')) {
            return 0;
        }
    }
    return 1;
}

int ph_tls_init(ph_tls_ctx_t *ctx, int socket_fd, const char *sni_domain)
{
    if (!ctx || socket_fd < 0) return PH_ERR_NULL_PTR;
    memset(ctx, 0, sizeof(ph_tls_ctx_t));
    ctx->socket_fd = socket_fd;

    
    br_ssl_client_init_full(&ctx->sc, &ctx->xc, NULL, 0);
    
    
    br_ssl_engine_set_buffer(&ctx->sc.eng, ctx->iobuf, sizeof(ctx->iobuf), 1);
    
    
    const char *target_sni = sni_domain;
    if (!target_sni) target_sni = PH_TLS_SNI_DOMAIN;
    if (is_ip_address(target_sni)) target_sni = NULL;

    br_ssl_client_reset(&ctx->sc, target_sni, 0);

    
    br_sslio_init(&ctx->ioc, &ctx->sc.eng, sock_read, &ctx->socket_fd, sock_write, &ctx->socket_fd);

    return PH_OK;
}


int ph_tls_handshake(ph_tls_ctx_t *ctx)
{
    if (!ctx) return PH_ERR_NULL_PTR;

    
    br_sslio_flush(&ctx->ioc);

    uint32_t state;
    while ((state = br_ssl_engine_current_state(&ctx->sc.eng)) != BR_SSL_CLOSED) {
        if (!(state & BR_SSL_SENDREC) && !(state & BR_SSL_RECVREC)) {
            break;
        }
        if (br_sslio_flush(&ctx->ioc) < 0) break;
    }

    if (state & BR_SSL_CLOSED) {
        return PH_ERR_CRYPTO;
    }

    ctx->handshake_complete = 1;
    return PH_OK;
}

ssize_t ph_tls_send(ph_tls_ctx_t *ctx, const void *data, size_t len)
{
    if (!ctx || !ctx->handshake_complete) return -1;
    int wlen = br_sslio_write(&ctx->ioc, data, len);
    if (wlen < 0) return -1;
    br_sslio_flush(&ctx->ioc);
    return (ssize_t)wlen;
}

ssize_t ph_tls_recv(ph_tls_ctx_t *ctx, void *data, size_t len)
{
    if (!ctx || !ctx->handshake_complete) return -1;
    int rlen = br_sslio_read(&ctx->ioc, data, len);
    if (rlen < 0) return -1;
    return (ssize_t)rlen;
}

void ph_tls_cleanup(ph_tls_ctx_t *ctx)
{
    if (!ctx) return;
    br_sslio_close(&ctx->ioc);
    memset(ctx, 0, sizeof(ph_tls_ctx_t));
}
