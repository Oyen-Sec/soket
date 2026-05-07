#ifndef PH_ANTI_VM_H
#define PH_ANTI_VM_H

#include <stdbool.h>

/**
 * ph_anti_vm_check - Performs multiple checks to detect VM/Sandbox environments.
 * Returns true if a VM is detected, false otherwise.
 */
bool ph_anti_vm_check(void);

/**
 * ph_stalling_logic - Implements a stealthy random sleep to bypass automated analysis.
 */
void ph_stalling_logic(void);

#endif
