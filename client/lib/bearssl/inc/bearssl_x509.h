#ifndef BEARSSL_X509_H__
#define BEARSSL_X509_H__

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t vtable;
} br_x509_minimal_context;

void br_x509_minimal_init(br_x509_minimal_context *xc,
	const void *dn_hash_impl, const void *trust_anchors, size_t trust_anchors_num);

#endif
