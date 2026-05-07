
#ifndef SECURITY_MODULE_DETECT_H
#define SECURITY_MODULE_DETECT_H

#include "phantom.h"
#include <stdint.h>

typedef enum {
    PH_SEC_DISABLED = 0,
    PH_SEC_ENABLED,
    PH_SEC_ENFORCING,
    PH_SEC_PERMISSIVE,
    PH_SEC_UNKNOWN
} ph_sec_state_t;

typedef struct {
    int selinux_present;
    int selinux_enforcing;
    int apparmor_present;
    int apparmor_enforcing;
    int seccomp_present;
    ph_sec_state_t selinux_state;
    ph_sec_state_t apparmor_state;
    ph_sec_state_t seccomp_state;
    char selinux_context[256];
    char apparmor_profile[256];
} ph_security_info_t;

int ph_security_module_detect(ph_security_info_t *info);

int ph_security_check_selinux(ph_security_info_t *info);
int ph_security_check_apparmor(ph_security_info_t *info);
int ph_security_check_seccomp(ph_security_info_t *info);

const char* ph_security_state_string(ph_sec_state_t state);
int ph_security_is_restricted(ph_security_info_t *info);
const char* ph_security_get_summary(ph_security_info_t *info);

#endif
