#ifndef BEARSSL_SSL_H__
#define BEARSSL_SSL_H__

#include <stddef.h>
#include <stdint.h>

#define BR_SSL_BUFSIZE_BIDI   17408

typedef struct {
	uint32_t vtable;
} br_ssl_engine_context;

typedef struct {
	br_ssl_engine_context eng;
	// Simplified for this task
} br_ssl_client_context;

typedef struct {
	const br_ssl_engine_context *eng;
} br_sslio_context;

void br_ssl_client_init_full(br_ssl_client_context *cc,
	void *xc, const void *trust_anchors, size_t trust_anchors_num);

void br_ssl_engine_set_buffer(br_ssl_engine_context *cc,
	void *iobuf, size_t iobuf_len, int bidi);

void br_ssl_client_reset(br_ssl_client_context *cc,
	const char *server_name, int resume);

void br_sslio_init(br_sslio_context *ctx,
	br_ssl_engine_context *eng,
	int (*low_read)(void *read_context, unsigned char *data, size_t len),
	void *read_context,
	int (*low_write)(void *write_context, const unsigned char *data, size_t len),
	void *write_context);

int br_sslio_read(br_sslio_context *ctx, void *data, size_t len);
int br_sslio_write(br_sslio_context *ctx, const void *data, size_t len);
int br_sslio_flush(br_sslio_context *ctx);
int br_sslio_close(br_sslio_context *ctx);

uint32_t br_ssl_engine_current_state(const br_ssl_engine_context *cc);

#define BR_SSL_CLOSED       0x0001
#define BR_SSL_SENDREC      0x0002
#define BR_SSL_RECVREC      0x0004
#define BR_SSL_SENDAPP      0x0008
#define BR_SSL_RECVAPP      0x0010

#endif
