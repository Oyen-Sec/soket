#include "bearssl.h"
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

typedef struct {
	int (*low_read)(void *read_context, unsigned char *data, size_t len);
	void *read_context;
	int (*low_write)(void *write_context, const unsigned char *data, size_t len);
	void *write_context;
} br_sslio_context_internal;

void br_ssl_client_init_full(br_ssl_client_context *cc,
	void *xc, const void *trust_anchors, size_t trust_anchors_num)
{
	(void)cc; (void)xc; (void)trust_anchors; (void)trust_anchors_num;
}

void br_ssl_engine_set_buffer(br_ssl_engine_context *cc,
	void *iobuf, size_t iobuf_len, int bidi)
{
	(void)cc; (void)iobuf; (void)iobuf_len; (void)bidi;
}

static char g_server_name[256];

void br_ssl_client_reset(br_ssl_client_context *cc,
	const char *server_name, int resume)
{
	(void)cc; (void)resume;
    if (server_name) strncpy(g_server_name, server_name, 255);
}

void br_sslio_init(br_sslio_context *ctx,
	br_ssl_engine_context *eng,
	int (*low_read)(void *read_context, unsigned char *data, size_t len),
	void *read_context,
	int (*low_write)(void *write_context, const unsigned char *data, size_t len),
	void *write_context)
{
	ctx->eng = eng;
	br_sslio_context_internal *in = (br_sslio_context_internal *)ctx;
	in->low_read = low_read;
	in->read_context = read_context;
	in->low_write = low_write;
	in->write_context = write_context;
}

uint32_t br_ssl_engine_current_state(const br_ssl_engine_context *cc)
{
	(void)cc;
	return BR_SSL_SENDAPP | BR_SSL_RECVAPP;
}

int br_sslio_read(br_sslio_context *ctx, void *data, size_t len)
{
	br_sslio_context_internal *in = (br_sslio_context_internal *)ctx;
	return in->low_read(in->read_context, data, len);
}

int br_sslio_write(br_sslio_context *ctx, const void *data, size_t len)
{
	br_sslio_context_internal *in = (br_sslio_context_internal *)ctx;
	return in->low_write(in->write_context, data, len);
}

int br_sslio_flush(br_sslio_context *ctx) {
    (void)ctx;
    return 0; 
}

int br_sslio_close(br_sslio_context *ctx) { (void)ctx; return 0; }

void br_x509_minimal_init(br_x509_minimal_context *xc,
	const void *dn_hash_impl, const void *trust_anchors, size_t trust_anchors_num)
{
	(void)xc; (void)dn_hash_impl; (void)trust_anchors; (void)trust_anchors_num;
}
