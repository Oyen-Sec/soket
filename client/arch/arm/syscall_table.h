
#ifndef SYSCALL_TABLE_H
#define SYSCALL_TABLE_H

#define SYS_ph_read          3
#define SYS_ph_write         4
#define SYS_ph_open          5
#define SYS_ph_close         6
#define SYS_ph_wait4        114
#define SYS_ph_creat         8
#define SYS_ph_link          9
#define SYS_ph_unlink       10
#define SYS_ph_execve       11
#define SYS_ph_chdir        12
#define SYS_ph_time         13
#define SYS_ph_mknod        14
#define SYS_ph_chmod        15
#define SYS_ph_lchown       16
#define SYS_ph_getpid       20
#define SYS_ph_socketcall  102
#define SYS_ph_syslog      103
#define SYS_ph_setuid      23
#define SYS_ph_getuid      24
#define SYS_ph_geteuid     49
#ifndef SYS_prctl
#ifndef SYS_prctl
#define SYS_prctl          172
#endif
#endif
#define SYS_ph_memfd_create 385
#define SYS_ph_execveat    387

#define SYS_ph_ptrace      26
#define SYS_ph_seccomp     383

#define SYS_PH_SOCKET       1
#define SYS_PH_BIND         2
#define SYS_PH_CONNECT      3
#define SYS_PH_LISTEN       4
#define SYS_PH_ACCEPT       5
#define SYS_PH_GETSOCKNAME  6
#define SYS_PH_GETPEERNAME  7
#define SYS_PH_SOCKETPAIR   8
#define SYS_PH_SEND         9
#define SYS_PH_RECV        10
#define SYS_PH_SENDTO      11
#define SYS_PH_RECVFROM    12
#define SYS_PH_SETSOCKOPT  14

#define SYS_ph_dup         41
#define SYS_ph_dup2        63
#define SYS_ph_fcntl       55
#define SYS_ph_flock      143
#define SYS_ph_fsync      118
#define SYS_ph_epoll_create 250
#define SYS_ph_epoll_ctl    254
#define SYS_ph_epoll_wait   255

#define SYS_ph_gettimeofday 78
#define SYS_ph_nanosleep   162
#define SYS_ph_clock_gettime 263

#define SYS_ph_mmap2       192
#define SYS_ph_mprotect    125
#define SYS_ph_munmap       91
#define SYS_ph_brk         45

#endif
