
#include "syscall_ports.h"
#include "security_module_detect.h"
#include "utils.h"
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>

long ph_syscall_wrapper(long syscall_num, ...)
{
    va_list args;
    va_start(args, syscall_num);

    long arg1 = va_arg(args, long);
    long arg2 = va_arg(args, long);
    long arg3 = va_arg(args, long);
    long arg4 = va_arg(args, long);
    long arg5 = va_arg(args, long);
    long arg6 = va_arg(args, long);

    va_end(args);

    long ret = syscall(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6);

    if (ret == -1 && errno == ENOSYS) {
    }

    return ret;
}

int ph_syscall_memfd_create(const char *name, unsigned int flags)
{
    if (!name) {
        return PH_ERR_NULL_PTR;
    }

    int ret = (int)syscall(SYS_ph_memfd_create, name, flags);

    if (ret == -1) {
        if (errno == ENOSYS) {
            return PH_ERR_INVALID_ARG;
        }
        return PH_ERR_SOCKET;
    }

    return ret;
}

int ph_syscall_execveat(int dirfd, const char *pathname,
                        char *const argv[], char *const envp[],
                        int flags)
{
    if (!pathname) {
        return PH_ERR_NULL_PTR;
    }

    int ret = (int)syscall(SYS_ph_execveat, dirfd, pathname, argv, envp, flags);

    if (ret == -1) {
        if (errno == ENOSYS) {
            return PH_ERR_INVALID_ARG;
        }
        return PH_ERR_SOCKET;
    }

    return ret;
}

int ph_kernel_detect_features(ph_kernel_features_t *features)
{
    if (!features) {
        return PH_ERR_NULL_PTR;
    }

    struct utsname uts;
    int ret = uname(&uts);
    if (ret != 0) {
        return PH_ERR_INVALID_ARG;
    }

    int major = 0, minor = 0, patch = 0;
    const char *p = uts.release;

    while (*p >= '0' && *p <= '9') {
        major = major * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') p++;

    while (*p >= '0' && *p <= '9') {
        minor = minor * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') p++;

    while (*p >= '0' && *p <= '9') {
        patch = patch * 10 + (*p - '0');
        p++;
    }

    features->major_version = major;
    features->minor_version = minor;
    features->patch_version = patch;

    strncpy(features->release, uts.release, sizeof(features->release) - 1);
    features->release[sizeof(features->release) - 1] = '\0';

    strncpy(features->machine, uts.machine, sizeof(features->machine) - 1);
    features->machine[sizeof(features->machine) - 1] = '\0';

    features->has_memfd_create = (major > 3 || (major == 3 && minor >= 17));
    features->has_execveat = (major > 3 || (major == 3 && minor >= 19));
    features->has_getrandom = (major > 3 || (major == 3 && minor >= 17));
    features->has_epoll = (major > 2 || (major == 2 && minor >= 6));
    features->has_pidfd_open = (major >= 5 && minor >= 3);

    return PH_OK;
}

int ph_kernel_check_version(int major, int minor, int patch)
{
    ph_kernel_features_t features;
    int ret = ph_kernel_detect_features(&features);
    if (ret != PH_OK) {
        return 0;
    }

    if (features.major_version > major) {
        return 1;
    }
    if (features.major_version == major && features.minor_version > minor) {
        return 1;
    }
    if (features.major_version == major && features.minor_version == minor &&
        features.patch_version >= patch) {
        return 1;
    }

    return 0;
}

const char* ph_kernel_get_release(void)
{
    static char release[256] = {0};
    struct utsname uts;

    if (uname(&uts) == 0) {
        strncpy(release, uts.release, sizeof(release) - 1);
    }

    return release;
}

const char* ph_kernel_get_machine(void)
{
    static char machine[256] = {0};
    struct utsname uts;

    if (uname(&uts) == 0) {
        strncpy(machine, uts.machine, sizeof(machine) - 1);
    }

    return machine;
}

int ph_security_detect(ph_security_status_t *status)
{
    if (!status) {
        return PH_ERR_NULL_PTR;
    }

    memset(status, 0, sizeof(ph_security_status_t));

    if (ph_security_is_selinux()) {
        status->module = PH_SECURITY_SELINUX;
        status->is_available = 1;
        status->is_enforcing = 1;
        strncpy(status->profile_name, "SELinux", sizeof(status->profile_name) - 1);
        return PH_OK;
    }

    if (ph_security_is_apparmor()) {
        status->module = PH_SECURITY_APPARMOR;
        status->is_available = 1;
        status->is_enforcing = 1;
        strncpy(status->profile_name, "AppArmor", sizeof(status->profile_name) - 1);
        return PH_OK;
    }

    if (ph_security_is_seccomp()) {
        status->module = PH_SECURITY_SECCOMP;
        status->is_available = 1;
        status->is_enforcing = 1;
        strncpy(status->profile_name, "Seccomp", sizeof(status->profile_name) - 1);
        return PH_OK;
    }

    status->module = PH_SECURITY_NONE;
    status->is_available = 0;
    status->is_enforcing = 0;
    strncpy(status->profile_name, "None", sizeof(status->profile_name) - 1);

    return PH_OK;
}

int ph_security_is_selinux(void)
{

    if (access("/sys/fs/selinux", F_OK) == 0) {
        return 1;
    }

    if (access("/etc/selinux", F_OK) == 0) {
        return 1;
    }

    return 0;
}

int ph_security_is_apparmor(void)
{

    if (access("/sys/module/apparmor", F_OK) == 0) {
        return 1;
    }

    if (access("/etc/apparmor.d", F_OK) == 0) {
        return 1;
    }

    return 0;
}

int ph_security_is_seccomp(void)
{

    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char buffer[4096];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0) return 0;
    buffer[n] = '\0';

    char *seccomp_line = strstr(buffer, "Seccomp:");
    if (!seccomp_line) return 0;

    seccomp_line += 8;
    while (*seccomp_line == ' ' || *seccomp_line == '\t') seccomp_line++;

    int seccomp_value = 0;
    while (*seccomp_line >= '0' && *seccomp_line <= '9') {
        seccomp_value = seccomp_value * 10 + (*seccomp_line - '0');
        seccomp_line++;
    }

    return (seccomp_value > 0) ? 1 : 0;
}

const char* ph_security_module_name(ph_security_module_t module)
{
    switch (module) {
        case PH_SECURITY_SELINUX:
            return "SELinux";
        case PH_SECURITY_APPARMOR:
            return "AppArmor";
        case PH_SECURITY_SECCOMP:
            return "Seccomp";
        case PH_SECURITY_NONE:
            return "None";
        default:
            return "Unknown";
    }
}

const char* ph_security_status_string(ph_security_status_t *status)
{
    if (!status) {
        return "Invalid";
    }

    static char status_str[512];
    char *ptr = status_str;
    const char *prefix = "Module: ";
    size_t len = 8;
    memcpy(ptr, prefix, len);
    ptr += len;

    const char *mod_name = ph_security_module_name(status->module);
    size_t mod_len = strlen(mod_name);
    memcpy(ptr, mod_name, mod_len);
    ptr += mod_len;

    const char *avail = ", Available: ";
    len = 13;
    memcpy(ptr, avail, len);
    ptr += len;

    const char *yes_no = status->is_available ? "YES" : "NO";
    len = 3;
    memcpy(ptr, yes_no, len);
    ptr += len;

    const char *enforcing = ", Enforcing: ";
    len = 12;
    memcpy(ptr, enforcing, len);
    ptr += len;

    yes_no = status->is_enforcing ? "YES" : "NO";
    memcpy(ptr, yes_no, 3);
    ptr += 3;

    *ptr = '\0';

    return status_str;
}

const char* ph_arch_detect(void)
{
    static char arch[64] = {0};
    struct utsname uts;

    if (uname(&uts) == 0) {
        strncpy(arch, uts.machine, sizeof(arch) - 1);
    }

    return arch;
}

int ph_arch_is_64bit(void)
{
    const char *arch = ph_arch_detect();

    if (strstr(arch, "x86_64") || strstr(arch, "aarch64") ||
        strstr(arch, "ppc64") || strstr(arch, "mips64")) {
        return 1;
    }

    return 0;
}
