#include "anti_vm.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __x86_64__
static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
                                unsigned int *ecx, unsigned int *edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "0"(*eax), "2"(*ecx));
}
#endif

bool ph_check_cpuid(void) {
#ifdef __x86_64__
    unsigned int eax, ebx, ecx, edx;
    
    // Hypervisor bit check (EAX=1)
    eax = 1;
    ecx = 0;
    native_cpuid(&eax, &ebx, &ecx, &edx);
    if ((ecx >> 31) & 1) return true;

    // Hypervisor brand check
    eax = 0x40000000;
    ecx = 0;
    native_cpuid(&eax, &ebx, &ecx, &edx);
    char hyper_vendor[13];
    memcpy(hyper_vendor + 0, &ebx, 4);
    memcpy(hyper_vendor + 4, &edx, 4);
    memcpy(hyper_vendor + 8, &ecx, 4);
    hyper_vendor[12] = '\0';

    const char *vm_vendors[] = {"VMwareVMware", "Microsoft Hv", "KVMKVMKVM", "XenVMMXenVMM", "prl hyperv", "VBoxVBoxVBox"};
    for (int i = 0; i < 6; i++) {
        if (strcmp(hyper_vendor, vm_vendors[i]) == 0) return true;
    }
#endif
    return false;
}

bool ph_check_files(void) {
    const char *vm_files[] = {
        "/usr/bin/VBoxControl",
        "/usr/bin/VBoxService",
        "/usr/libexec/qemu-ga",
        "/proc/scsi/virtio_scsi",
        "/sys/module/virtio"
    };
    struct stat st;
    for (int i = 0; i < 5; i++) {
        if (stat(vm_files[i], &st) == 0) return true;
    }
    return false;
}

bool ph_anti_vm_check(void) {
    if (ph_check_cpuid()) return true;
    if (ph_check_files()) return true;
    return false;
}

void ph_stalling_logic(void) {
    srand(time(NULL));
    // Random sleep between 30 and 120 seconds for production stealth
    int delay = 30 + (rand() % 90);
    sleep(delay);
}
