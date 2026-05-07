
#include "security_module_detect.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

static int read_int_from_file(const char *path, int *value)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) return -1;
    buf[n] = '\0';

    *value = 0;
    for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; i++) {
        *value = *value * 10 + (buf[i] - '0');
    }
    return 0;
}

static int read_file_line(const char *path, char *buf, size_t buf_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    ssize_t n = read(fd, buf, buf_size - 1);
    close(fd);

    if (n <= 0) return -1;
    buf[n] = '\0';

    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

    return 0;
}

int ph_security_module_detect(ph_security_info_t *info)
{
    if (!info) {
        return PH_ERR_NULL_PTR;
    }

    memset(info, 0, sizeof(ph_security_info_t));

    ph_security_check_selinux(info);

    ph_security_check_apparmor(info);

    ph_security_check_seccomp(info);

    return PH_OK;
}

int ph_security_check_selinux(ph_security_info_t *info)
{
    if (!info) {
        return PH_ERR_NULL_PTR;
    }

    if (access("/sys/fs/selinux", F_OK) != 0) {
        info->selinux_present = 0;
        info->selinux_state = PH_SEC_DISABLED;
        return PH_OK;
    }

    info->selinux_present = 1;

    int enforcing = 0;
    if (read_int_from_file("/sys/fs/selinux/enforce", &enforcing) == 0) {
        info->selinux_enforcing = (enforcing == 1) ? 1 : 0;
        info->selinux_state = enforcing ? PH_SEC_ENFORCING : PH_SEC_PERMISSIVE;
    }

    read_file_line("/proc/self/attr/current", info->selinux_context, sizeof(info->selinux_context));

    return PH_OK;
}

int ph_security_check_apparmor(ph_security_info_t *info)
{
    if (!info) {
        return PH_ERR_NULL_PTR;
    }

    if (access("/sys/module/apparmor", F_OK) != 0) {
        info->apparmor_present = 0;
        info->apparmor_state = PH_SEC_DISABLED;
        return PH_OK;
    }

    info->apparmor_present = 1;

    int fd = open("/sys/kernel/security/apparmor/profiles", O_RDONLY);
    if (fd >= 0) {
        char buffer[4096];
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);

        if (n > 0) {
            buffer[n] = '\0';
            info->apparmor_enforcing = 1;
            info->apparmor_state = PH_SEC_ENFORCING;

            char *newline = strchr(buffer, '\n');
            if (newline) *newline = '\0';
            char *space = strchr(buffer, ' ');
            if (space) *space = '\0';

            strncpy(info->apparmor_profile, buffer, sizeof(info->apparmor_profile) - 1);
        }
    }

    return PH_OK;
}

int ph_security_check_seccomp(ph_security_info_t *info)
{
    if (!info) {
        return PH_ERR_NULL_PTR;
    }

    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0) {
        info->seccomp_present = 0;
        info->seccomp_state = PH_SEC_UNKNOWN;
        return PH_ERR_INVALID_ARG;
    }

    char buffer[4096];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0) {
        info->seccomp_present = 0;
        info->seccomp_state = PH_SEC_UNKNOWN;
        return PH_ERR_INVALID_ARG;
    }

    buffer[n] = '\0';

    int seccomp_value = 0;
    char *seccomp_line = strstr(buffer, "Seccomp:");
    if (seccomp_line) {
        seccomp_line += 8;
        while (*seccomp_line == ' ' || *seccomp_line == '\t') seccomp_line++;

        seccomp_value = 0;
        while (*seccomp_line >= '0' && *seccomp_line <= '9') {
            seccomp_value = seccomp_value * 10 + (*seccomp_line - '0');
            seccomp_line++;
        }
    }

    info->seccomp_present = (seccomp_value > 0) ? 1 : 0;

    if (seccomp_value == 0) {
        info->seccomp_state = PH_SEC_DISABLED;
    } else if (seccomp_value == 1) {
        info->seccomp_state = PH_SEC_ENFORCING;
    } else if (seccomp_value == 2) {
        info->seccomp_state = PH_SEC_ENFORCING;
    } else {
        info->seccomp_state = PH_SEC_UNKNOWN;
    }

    return PH_OK;
}

const char* ph_security_state_string(ph_sec_state_t state)
{
    switch (state) {
        case PH_SEC_DISABLED:
            return "Disabled";
        case PH_SEC_ENABLED:
            return "Enabled";
        case PH_SEC_ENFORCING:
            return "Enforcing";
        case PH_SEC_PERMISSIVE:
            return "Permissive";
        case PH_SEC_UNKNOWN:
            return "Unknown";
        default:
            return "Invalid";
    }
}

int ph_security_is_restricted(ph_security_info_t *info)
{
    if (!info) {
        return 0;
    }

    if (info->selinux_enforcing || info->apparmor_enforcing ||
        info->seccomp_state == PH_SEC_ENFORCING) {
        return 1;
    }

    return 0;
}

const char* ph_security_get_summary(ph_security_info_t *info)
{
    if (!info) {
        return "Invalid";
    }

    static char summary[1024];
    char *ptr = summary;

    const char *selinux_prefix = "SELinux: ";
    size_t len = 9;
    memcpy(ptr, selinux_prefix, len);
    ptr += len;

    const char *present = info->selinux_present ? "Present" : "Absent";
    len = strlen(present);
    memcpy(ptr, present, len);
    ptr += len;

    const char *sep = " (";
    len = 2;
    memcpy(ptr, sep, len);
    ptr += len;

    const char *state = ph_security_state_string(info->selinux_state);
    len = strlen(state);
    memcpy(ptr, state, len);
    ptr += len;

    *ptr++ = ')';

    const char *apparmor_prefix = ", AppArmor: ";
    len = 12;
    memcpy(ptr, apparmor_prefix, len);
    ptr += len;

    present = info->apparmor_present ? "Present" : "Absent";
    len = strlen(present);
    memcpy(ptr, present, len);
    ptr += len;

    sep = " (";
    len = 2;
    memcpy(ptr, sep, len);
    ptr += len;

    state = ph_security_state_string(info->apparmor_state);
    len = strlen(state);
    memcpy(ptr, state, len);
    ptr += len;

    *ptr++ = ')';

    const char *seccomp_prefix = ", Seccomp: ";
    len = 11;
    memcpy(ptr, seccomp_prefix, len);
    ptr += len;

    present = info->seccomp_present ? "Present" : "Absent";
    len = strlen(present);
    memcpy(ptr, present, len);
    ptr += len;

    sep = " (";
    len = 2;
    memcpy(ptr, sep, len);
    ptr += len;

    state = ph_security_state_string(info->seccomp_state);
    len = strlen(state);
    memcpy(ptr, state, len);
    ptr += len;

    *ptr++ = ')';
    *ptr = '\0';

    return summary;
}
