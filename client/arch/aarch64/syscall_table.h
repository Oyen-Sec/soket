
#ifndef SYSCALL_TABLE_H
#define SYSCALL_TABLE_H

#define SYS_ph_read          63
#define SYS_ph_write         64
#define SYS_ph_open          56
#define SYS_ph_openat        56
#define SYS_ph_close         57
#define SYS_ph_stat         106
#define SYS_ph_fstat         80
#define SYS_ph_lstat        107
#define SYS_ph_poll         209
#define SYS_ph_lseek         62
#define SYS_ph_mmap         222
#define SYS_ph_mprotect     226
#define SYS_ph_munmap       215
#define SYS_ph_brk          214
#define SYS_ph_rt_sigaction 134
#define SYS_ph_rt_sigprocmask 135
#define SYS_ph_ioctl        29
#define SYS_ph_access       48
#define SYS_ph_pipe2       59
#define SYS_ph_select       72
#define SYS_ph_socket      198
#define SYS_ph_connect     203
#define SYS_ph_accept      202
#define SYS_ph_accept4     202
#define SYS_ph_sendto      206
#define SYS_ph_recvfrom    207
#define SYS_ph_setsockopt  204
#define SYS_ph_clone        56
#define SYS_ph_execve      221
#define SYS_ph_exit         93
#define SYS_ph_wait4       260
#define SYS_ph_kill        129
#define SYS_ph_getcwd       17
#define SYS_ph_fcntl        25
#define SYS_ph_flock       228
#define SYS_ph_fsync        82
#define SYS_ph_getpid      172
#define SYS_ph_getuid      174
#define SYS_ph_geteuid     175
#ifndef SYS_prctl
#ifndef SYS_prctl
#define SYS_prctl          167
#endif
#endif
#define SYS_ph_memfd_create 279
#define SYS_ph_execveat    281

#define SYS_ph_ptrace      117
#define SYS_ph_seccomp     277

#define SYS_ph_bind        200
#define SYS_ph_listen      201
#define SYS_ph_getsockname 204
#define SYS_ph_getpeername 205
#define SYS_ph_socketpair  199

#define SYS_ph_dup          23
#define SYS_ph_dup2         24
#define SYS_ph_epoll_create1 21
#define SYS_ph_epoll_ctl    21
#define SYS_ph_epoll_wait   21

#define SYS_ph_gettimeofday 169
#define SYS_ph_nanosleep   101
#define SYS_ph_clock_gettime 113

#endif
