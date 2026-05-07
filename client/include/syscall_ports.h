
#ifndef SYSCALL_PORTS_H
#define SYSCALL_PORTS_H

#include "phantom.h"
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

#include "syscall_table.h"

typedef struct {
    int has_memfd_create;
    int has_execveat;
    int has_getrandom;
    int has_epoll;
    int has_pidfd_open;
    int major_version;
    int minor_version;
    int patch_version;
    char release[256];
    char machine[256];
} ph_kernel_features_t;

typedef enum {
    PH_SECURITY_NONE = 0,
    PH_SECURITY_SELINUX,
    PH_SECURITY_APPARMOR,
    PH_SECURITY_SECCOMP,
    PH_SECURITY_UNKNOWN
} ph_security_module_t;

typedef struct {
    ph_security_module_t module;
    int is_enforcing;
    int is_available;
    char profile_name[256];
} ph_security_status_t;

long ph_syscall_wrapper(long syscall_num, ...);
int ph_syscall_memfd_create(const char *name, unsigned int flags);
int ph_syscall_execveat(int dirfd, const char *pathname,
                        char *const argv[], char *const envp[],
                        int flags);

int ph_kernel_detect_features(ph_kernel_features_t *features);
int ph_kernel_check_version(int major, int minor, int patch);
const char* ph_kernel_get_release(void);
const char* ph_kernel_get_machine(void);

int ph_security_detect(ph_security_status_t *status);
int ph_security_is_selinux(void);
int ph_security_is_apparmor(void);
int ph_security_is_seccomp(void);
const char* ph_security_module_name(ph_security_module_t module);
const char* ph_security_status_string(ph_security_status_t *status);

const char* ph_arch_detect(void);
int ph_arch_is_64bit(void);

#endif
