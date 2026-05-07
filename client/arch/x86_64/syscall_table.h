
#ifndef SYSCALL_TABLE_H
#define SYSCALL_TABLE_H

#define SYS_ph_read          0
#define SYS_ph_write         1
#define SYS_ph_open          2
#define SYS_ph_close         3
#define SYS_ph_stat          4
#define SYS_ph_fstat         5
#define SYS_ph_lstat         6
#define SYS_ph_poll          7
#define SYS_ph_lseek         8
#define SYS_ph_mmap          9
#define SYS_ph_mprotect     10
#define SYS_ph_munmap       11
#define SYS_ph_brk          12
#define SYS_ph_rt_sigaction 13
#define SYS_ph_rt_sigprocmask 14
#define SYS_ph_ioctl        16
#define SYS_ph_access       21
#define SYS_ph_pipe         22
#define SYS_ph_select       23
#define SYS_ph_socket       41
#define SYS_ph_connect      42
#define SYS_ph_accept       43
#define SYS_ph_sendto       44
#define SYS_ph_recvfrom     45
#define SYS_ph_setsockopt   50
#define SYS_ph_clone        56
#define SYS_ph_fork         57
#define SYS_ph_vfork        58
#define SYS_ph_execve       59
#define SYS_ph_exit         60
#define SYS_ph_wait4        61
#define SYS_ph_kill         62
#define SYS_ph_getcwd       79
#define SYS_ph_fcntl        72
#define SYS_ph_flock        73
#define SYS_ph_fsync        74
#define SYS_ph_getpid       39
#define SYS_ph_getuid       102
#define SYS_ph_geteuid      107
#ifndef SYS_prctl
#ifndef SYS_prctl
#define SYS_prctl           157
#endif
#endif
#define SYS_ph_memfd_create 319
#define SYS_ph_execveat     322

#define SYS_ph_ptrace       101
#define SYS_ph_seccomp      317

#define SYS_ph_bind         49
#define SYS_ph_listen       50
#define SYS_ph_getsockname  51
#define SYS_ph_getpeername  52
#define SYS_ph_socketpair   53

#define SYS_ph_dup          32
#define SYS_ph_dup2         33
#define SYS_ph_epoll_create1 213
#define SYS_ph_epoll_ctl    233
#define SYS_ph_epoll_wait   232

#define SYS_ph_gettimeofday 96
#define SYS_ph_nanosleep    35
#define SYS_ph_clock_gettime 228

#endif
