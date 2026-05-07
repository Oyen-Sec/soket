
#ifndef UTILS_H
#define UTILS_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

void ph_memset_s(void *ptr, int value, size_t len);
void ph_xor_obfuscate(char *str, size_t len);
int ph_safe_strcmp(const char *s1, const char *s2);

uint64_t ph_get_timestamp_ms(void);
void ph_sleep_ms(uint32_t milliseconds);
void ph_jitter_sleep(uint32_t min_ms, uint32_t max_ms);

void *ph_malloc(size_t size);
void ph_free(void *ptr);
void ph_wipe_memory(void *ptr, size_t size);

void ph_log_info(const char *fmt, ...);
void ph_log_error(const char *fmt, ...);
void ph_log_debug(const char *fmt, ...);

void *ph_monitor_thread(void *arg);

#endif
