
#ifndef TLS_WRAPPER_H
#define TLS_WRAPPER_H

#include "phantom.h"
#include "bearssl.h"
#include <stdint.h>
#include <stddef.h>

#define PH_TLS_SNI_DOMAIN "bootoyen.duckdns.org"
#define PH_TLS_CERT_HASH "4845322d3b0b6e58e0b359157f8b31b82acfe9850452d876000a944332750244" 

typedef struct {
    int socket_fd;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    int handshake_complete;
} ph_tls_ctx_t;

int ph_tls_init(ph_tls_ctx_t *ctx, int socket_fd, const char *sni_domain);
int ph_tls_handshake(ph_tls_ctx_t *ctx);
ssize_t ph_tls_send(ph_tls_ctx_t *ctx, const void *data, size_t len);
ssize_t ph_tls_recv(ph_tls_ctx_t *ctx, void *data, size_t len);
void ph_tls_cleanup(ph_tls_ctx_t *ctx);

#endif
