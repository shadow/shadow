/// This file is generated with the 'gen_syscall_wrappers_c.py' script and in
/// general SHOULD NOT be edited manually.
///
/// This file contains a symbol for every system call (i.e., in man section 2;
/// see `man man`). Those for which a syscall wrapper function exists in libc
/// will be intercepted and redirected to `syscall()`.
///
/// NOTE: defining a syscall here does not always mean it's handled by Shadow.
/// See `src/main/host/syscall_handler.c` for the syscalls that Shadow handles.

// To get the INTERPOSE defs. Do not include other headers to avoid conflicts.
#include "interpose.h"

// clang-format off
#ifdef SYS__sysctl // kernel entry: num=156 func=sys_ni_syscall
INTERPOSE(_sysctl);
#endif
#ifdef SYS_accept // kernel entry: num=43 func=sys_accept
INTERPOSE(accept);
#endif
#ifdef SYS_accept4 // kernel entry: num=288 func=sys_accept4
INTERPOSE(accept4);
#endif
#ifdef SYS_access // kernel entry: num=21 func=sys_access
INTERPOSE(access);
#endif
#ifdef SYS_acct // kernel entry: num=163 func=sys_acct
INTERPOSE(acct);
#endif
#ifdef SYS_add_key // kernel entry: num=248 func=sys_add_key
INTERPOSE(add_key);
#endif
#ifdef SYS_adjtimex // kernel entry: num=159 func=sys_adjtimex
INTERPOSE(adjtimex);
#endif
#ifdef SYS_alarm // kernel entry: num=37 func=sys_alarm
INTERPOSE(alarm);
#endif
#ifdef SYS_arch_prctl // kernel entry: num=158 func=sys_arch_prctl
INTERPOSE(arch_prctl);
#endif
#ifdef SYS_bind // kernel entry: num=49 func=sys_bind
INTERPOSE(bind);
#endif
#ifdef SYS_bpf // kernel entry: num=321 func=sys_bpf
INTERPOSE(bpf);
#endif
// Skipping SYS_brk
#ifdef SYS_cachestat // kernel entry: num=451 func=sys_cachestat
INTERPOSE(cachestat);
#endif
#ifdef SYS_capget // kernel entry: num=125 func=sys_capget
INTERPOSE(capget);
#endif
#ifdef SYS_capset // kernel entry: num=126 func=sys_capset
INTERPOSE(capset);
#endif
#ifdef SYS_chdir // kernel entry: num=80 func=sys_chdir
INTERPOSE(chdir);
#endif
// Skipping SYS_chmod
#ifdef SYS_chown // kernel entry: num=92 func=sys_chown
INTERPOSE(chown);
#endif
#ifdef SYS_chroot // kernel entry: num=161 func=sys_chroot
INTERPOSE(chroot);
#endif
#ifdef SYS_clock_adjtime // kernel entry: num=305 func=sys_clock_adjtime
INTERPOSE(clock_adjtime);
#endif
#ifdef SYS_clock_getres // kernel entry: num=229 func=sys_clock_getres
INTERPOSE(clock_getres);
#endif
#ifdef SYS_clock_gettime // kernel entry: num=228 func=sys_clock_gettime
INTERPOSE(clock_gettime);
#endif
#ifdef SYS_clock_nanosleep // kernel entry: num=230 func=sys_clock_nanosleep
INTERPOSE_DIRECT_ERRORS(clock_nanosleep);
#endif
#ifdef SYS_clock_settime // kernel entry: num=227 func=sys_clock_settime
INTERPOSE(clock_settime);
#endif
// Skipping SYS_clone
// Skipping SYS_clone3
#ifdef SYS_close // kernel entry: num=3 func=sys_close
INTERPOSE(close);
#endif
// Skipping SYS_close_range
#ifdef SYS_connect // kernel entry: num=42 func=sys_connect
INTERPOSE(connect);
#endif
#ifdef SYS_copy_file_range // kernel entry: num=326 func=sys_copy_file_range
INTERPOSE(copy_file_range);
#endif
#ifdef SYS_creat // kernel entry: num=85 func=sys_creat
INTERPOSE(creat);
#endif
#ifdef SYS_delete_module // kernel entry: num=176 func=sys_delete_module
INTERPOSE(delete_module);
#endif
#ifdef SYS_dup // kernel entry: num=32 func=sys_dup
INTERPOSE(dup);
#endif
#ifdef SYS_dup2 // kernel entry: num=33 func=sys_dup2
INTERPOSE(dup2);
#endif
#ifdef SYS_dup3 // kernel entry: num=292 func=sys_dup3
INTERPOSE(dup3);
#endif
#ifdef SYS_epoll_create // kernel entry: num=213 func=sys_epoll_create
INTERPOSE(epoll_create);
#endif
#ifdef SYS_epoll_create1 // kernel entry: num=291 func=sys_epoll_create1
INTERPOSE(epoll_create1);
#endif
#ifdef SYS_epoll_ctl // kernel entry: num=233 func=sys_epoll_ctl
INTERPOSE(epoll_ctl);
#endif
// Skipping SYS_epoll_pwait
// Skipping SYS_epoll_pwait2
#ifdef SYS_epoll_wait // kernel entry: num=232 func=sys_epoll_wait
INTERPOSE(epoll_wait);
#endif
#ifdef SYS_eventfd // kernel entry: num=284 func=sys_eventfd
INTERPOSE_REMAP(eventfd, eventfd2);
#endif
// Skipping SYS_eventfd2
#ifdef SYS_execve // kernel entry: num=59 func=sys_execve
INTERPOSE(execve);
#endif
#ifdef SYS_execveat // kernel entry: num=322 func=sys_execveat
INTERPOSE(execveat);
#endif
// Skipping SYS_exit
#ifdef SYS_exit_group // kernel entry: num=231 func=sys_exit_group
INTERPOSE(exit_group);
#endif
// Skipping SYS_faccessat
// Skipping SYS_faccessat2
// Skipping SYS_fadvise64
#ifdef SYS_fallocate // kernel entry: num=285 func=sys_fallocate
INTERPOSE(fallocate);
#endif
#ifdef SYS_fanotify_init // kernel entry: num=300 func=sys_fanotify_init
INTERPOSE(fanotify_init);
#endif
#ifdef SYS_fanotify_mark // kernel entry: num=301 func=sys_fanotify_mark
INTERPOSE(fanotify_mark);
#endif
#ifdef SYS_fchdir // kernel entry: num=81 func=sys_fchdir
INTERPOSE(fchdir);
#endif
// Skipping SYS_fchmod
// Skipping SYS_fchmodat
// Skipping SYS_fchmodat2
#ifdef SYS_fchown // kernel entry: num=93 func=sys_fchown
INTERPOSE(fchown);
#endif
#ifdef SYS_fchownat // kernel entry: num=260 func=sys_fchownat
INTERPOSE(fchownat);
#endif
#ifdef SYS_fcntl // kernel entry: num=72 func=sys_fcntl
INTERPOSE(fcntl);
#endif
#ifdef SYS_fdatasync // kernel entry: num=75 func=sys_fdatasync
INTERPOSE(fdatasync);
#endif
#ifdef SYS_fgetxattr // kernel entry: num=193 func=sys_fgetxattr
INTERPOSE(fgetxattr);
#endif
#ifdef SYS_file_getattr // kernel entry: num=468 func=sys_file_getattr
INTERPOSE(file_getattr);
#endif
#ifdef SYS_file_setattr // kernel entry: num=469 func=sys_file_setattr
INTERPOSE(file_setattr);
#endif
#ifdef SYS_finit_module // kernel entry: num=313 func=sys_finit_module
INTERPOSE(finit_module);
#endif
#ifdef SYS_flistxattr // kernel entry: num=196 func=sys_flistxattr
INTERPOSE(flistxattr);
#endif
#ifdef SYS_flock // kernel entry: num=73 func=sys_flock
INTERPOSE(flock);
#endif
// Skipping SYS_fork
#ifdef SYS_fremovexattr // kernel entry: num=199 func=sys_fremovexattr
INTERPOSE(fremovexattr);
#endif
// Skipping SYS_fsconfig
#ifdef SYS_fsetxattr // kernel entry: num=190 func=sys_fsetxattr
INTERPOSE(fsetxattr);
#endif
// Skipping SYS_fsmount
// Skipping SYS_fsopen
// Skipping SYS_fspick
#ifdef SYS_fstat // kernel entry: num=5 func=sys_newfstat
INTERPOSE(fstat);
#endif
#ifdef SYS_fstatfs // kernel entry: num=138 func=sys_fstatfs
INTERPOSE(fstatfs);
#endif
#ifdef SYS_fsync // kernel entry: num=74 func=sys_fsync
INTERPOSE(fsync);
#endif
#ifdef SYS_ftruncate // kernel entry: num=77 func=sys_ftruncate
INTERPOSE(ftruncate);
#endif
#ifdef SYS_futex // kernel entry: num=202 func=sys_futex
INTERPOSE(futex);
#endif
#ifdef SYS_futex_requeue // kernel entry: num=456 func=sys_futex_requeue
INTERPOSE(futex_requeue);
#endif
#ifdef SYS_futex_wait // kernel entry: num=455 func=sys_futex_wait
INTERPOSE(futex_wait);
#endif
// Skipping SYS_futex_waitv
#ifdef SYS_futex_wake // kernel entry: num=454 func=sys_futex_wake
INTERPOSE(futex_wake);
#endif
#ifdef SYS_futimesat // kernel entry: num=261 func=sys_futimesat
INTERPOSE(futimesat);
#endif
#ifdef SYS_get_mempolicy // kernel entry: num=239 func=sys_get_mempolicy
INTERPOSE(get_mempolicy);
#endif
#ifdef SYS_get_robust_list // kernel entry: num=274 func=sys_get_robust_list
INTERPOSE(get_robust_list);
#endif
#ifdef SYS_getcpu // kernel entry: num=309 func=sys_getcpu
INTERPOSE(getcpu);
#endif
// Skipping SYS_getcwd
// Skipping SYS_getdents
// Skipping SYS_getdents64
#ifdef SYS_getegid // kernel entry: num=108 func=sys_getegid
INTERPOSE(getegid);
#endif
#ifdef SYS_geteuid // kernel entry: num=107 func=sys_geteuid
INTERPOSE(geteuid);
#endif
#ifdef SYS_getgid // kernel entry: num=104 func=sys_getgid
INTERPOSE(getgid);
#endif
// Skipping SYS_getgroups
#ifdef SYS_getitimer // kernel entry: num=36 func=sys_getitimer
INTERPOSE(getitimer);
#endif
#ifdef SYS_getpeername // kernel entry: num=52 func=sys_getpeername
INTERPOSE(getpeername);
#endif
#ifdef SYS_getpgid // kernel entry: num=121 func=sys_getpgid
INTERPOSE(getpgid);
#endif
#ifdef SYS_getpgrp // kernel entry: num=111 func=sys_getpgrp
INTERPOSE(getpgrp);
#endif
#ifdef SYS_getpid // kernel entry: num=39 func=sys_getpid
INTERPOSE(getpid);
#endif
#ifdef SYS_getppid // kernel entry: num=110 func=sys_getppid
INTERPOSE(getppid);
#endif
// Skipping SYS_getpriority
#ifdef SYS_getrandom // kernel entry: num=318 func=sys_getrandom
INTERPOSE(getrandom);
#endif
#ifdef SYS_getresgid // kernel entry: num=120 func=sys_getresgid
INTERPOSE(getresgid);
#endif
#ifdef SYS_getresuid // kernel entry: num=118 func=sys_getresuid
INTERPOSE(getresuid);
#endif
// Skipping SYS_getrlimit
#ifdef SYS_getrusage // kernel entry: num=98 func=sys_getrusage
INTERPOSE(getrusage);
#endif
#ifdef SYS_getsid // kernel entry: num=124 func=sys_getsid
INTERPOSE(getsid);
#endif
#ifdef SYS_getsockname // kernel entry: num=51 func=sys_getsockname
INTERPOSE(getsockname);
#endif
#ifdef SYS_getsockopt // kernel entry: num=55 func=sys_getsockopt
INTERPOSE(getsockopt);
#endif
#ifdef SYS_gettid // kernel entry: num=186 func=sys_gettid
INTERPOSE(gettid);
#endif
#ifdef SYS_gettimeofday // kernel entry: num=96 func=sys_gettimeofday
INTERPOSE(gettimeofday);
#endif
#ifdef SYS_getuid // kernel entry: num=102 func=sys_getuid
INTERPOSE(getuid);
#endif
#ifdef SYS_getxattr // kernel entry: num=191 func=sys_getxattr
INTERPOSE(getxattr);
#endif
#ifdef SYS_getxattrat // kernel entry: num=464 func=sys_getxattrat
INTERPOSE(getxattrat);
#endif
#ifdef SYS_init_module // kernel entry: num=175 func=sys_init_module
INTERPOSE(init_module);
#endif
#ifdef SYS_inotify_add_watch // kernel entry: num=254 func=sys_inotify_add_watch
INTERPOSE(inotify_add_watch);
#endif
#ifdef SYS_inotify_init // kernel entry: num=253 func=sys_inotify_init
INTERPOSE(inotify_init);
#endif
#ifdef SYS_inotify_init1 // kernel entry: num=294 func=sys_inotify_init1
INTERPOSE(inotify_init1);
#endif
#ifdef SYS_inotify_rm_watch // kernel entry: num=255 func=sys_inotify_rm_watch
INTERPOSE(inotify_rm_watch);
#endif
#ifdef SYS_io_cancel // kernel entry: num=210 func=sys_io_cancel
INTERPOSE(io_cancel);
#endif
#ifdef SYS_io_destroy // kernel entry: num=207 func=sys_io_destroy
INTERPOSE(io_destroy);
#endif
#ifdef SYS_io_getevents // kernel entry: num=208 func=sys_io_getevents
INTERPOSE(io_getevents);
#endif
// Skipping SYS_io_pgetevents
#ifdef SYS_io_setup // kernel entry: num=206 func=sys_io_setup
INTERPOSE(io_setup);
#endif
#ifdef SYS_io_submit // kernel entry: num=209 func=sys_io_submit
INTERPOSE(io_submit);
#endif
// Skipping SYS_io_uring_enter
// Skipping SYS_io_uring_register
// Skipping SYS_io_uring_setup
#ifdef SYS_ioctl // kernel entry: num=16 func=sys_ioctl
INTERPOSE(ioctl);
#endif
#ifdef SYS_ioperm // kernel entry: num=173 func=sys_ioperm
INTERPOSE(ioperm);
#endif
#ifdef SYS_iopl // kernel entry: num=172 func=sys_iopl
INTERPOSE(iopl);
#endif
#ifdef SYS_ioprio_get // kernel entry: num=252 func=sys_ioprio_get
INTERPOSE(ioprio_get);
#endif
#ifdef SYS_ioprio_set // kernel entry: num=251 func=sys_ioprio_set
INTERPOSE(ioprio_set);
#endif
// Skipping SYS_kcmp
// Skipping SYS_kexec_file_load
#ifdef SYS_kexec_load // kernel entry: num=246 func=sys_kexec_load
INTERPOSE(kexec_load);
#endif
#ifdef SYS_keyctl // kernel entry: num=250 func=sys_keyctl
INTERPOSE(keyctl);
#endif
#ifdef SYS_kill // kernel entry: num=62 func=sys_kill
INTERPOSE(kill);
#endif
// Skipping SYS_landlock_add_rule
// Skipping SYS_landlock_create_ruleset
// Skipping SYS_landlock_restrict_self
#ifdef SYS_lchown // kernel entry: num=94 func=sys_lchown
INTERPOSE(lchown);
#endif
#ifdef SYS_lgetxattr // kernel entry: num=192 func=sys_lgetxattr
INTERPOSE(lgetxattr);
#endif
#ifdef SYS_link // kernel entry: num=86 func=sys_link
INTERPOSE(link);
#endif
#ifdef SYS_linkat // kernel entry: num=265 func=sys_linkat
INTERPOSE(linkat);
#endif
#ifdef SYS_listen // kernel entry: num=50 func=sys_listen
INTERPOSE(listen);
#endif
#ifdef SYS_listmount // kernel entry: num=458 func=sys_listmount
INTERPOSE(listmount);
#endif
#ifdef SYS_listxattr // kernel entry: num=194 func=sys_listxattr
INTERPOSE(listxattr);
#endif
#ifdef SYS_listxattrat // kernel entry: num=465 func=sys_listxattrat
INTERPOSE(listxattrat);
#endif
#ifdef SYS_llistxattr // kernel entry: num=195 func=sys_llistxattr
INTERPOSE(llistxattr);
#endif
#ifdef SYS_lremovexattr // kernel entry: num=198 func=sys_lremovexattr
INTERPOSE(lremovexattr);
#endif
#ifdef SYS_lseek // kernel entry: num=8 func=sys_lseek
INTERPOSE(lseek);
#endif
#ifdef SYS_lsetxattr // kernel entry: num=189 func=sys_lsetxattr
INTERPOSE(lsetxattr);
#endif
#ifdef SYS_lsm_get_self_attr // kernel entry: num=459 func=sys_lsm_get_self_attr
INTERPOSE(lsm_get_self_attr);
#endif
#ifdef SYS_lsm_list_modules // kernel entry: num=461 func=sys_lsm_list_modules
INTERPOSE(lsm_list_modules);
#endif
#ifdef SYS_lsm_set_self_attr // kernel entry: num=460 func=sys_lsm_set_self_attr
INTERPOSE(lsm_set_self_attr);
#endif
#ifdef SYS_lstat // kernel entry: num=6 func=sys_newlstat
INTERPOSE(lstat);
#endif
#ifdef SYS_madvise // kernel entry: num=28 func=sys_madvise
INTERPOSE(madvise);
#endif
#ifdef SYS_map_shadow_stack // kernel entry: num=453 func=sys_map_shadow_stack
INTERPOSE(map_shadow_stack);
#endif
#ifdef SYS_mbind // kernel entry: num=237 func=sys_mbind
INTERPOSE(mbind);
#endif
#ifdef SYS_membarrier // kernel entry: num=324 func=sys_membarrier
INTERPOSE(membarrier);
#endif
#ifdef SYS_memfd_create // kernel entry: num=319 func=sys_memfd_create
INTERPOSE(memfd_create);
#endif
// Skipping SYS_memfd_secret
#ifdef SYS_migrate_pages // kernel entry: num=256 func=sys_migrate_pages
INTERPOSE(migrate_pages);
#endif
#ifdef SYS_mincore // kernel entry: num=27 func=sys_mincore
INTERPOSE(mincore);
#endif
#ifdef SYS_mkdir // kernel entry: num=83 func=sys_mkdir
INTERPOSE(mkdir);
#endif
#ifdef SYS_mkdirat // kernel entry: num=258 func=sys_mkdirat
INTERPOSE(mkdirat);
#endif
#ifdef SYS_mknod // kernel entry: num=133 func=sys_mknod
INTERPOSE(mknod);
#endif
#ifdef SYS_mknodat // kernel entry: num=259 func=sys_mknodat
INTERPOSE(mknodat);
#endif
#ifdef SYS_mlock // kernel entry: num=149 func=sys_mlock
INTERPOSE(mlock);
#endif
#ifdef SYS_mlock2 // kernel entry: num=325 func=sys_mlock2
INTERPOSE(mlock2);
#endif
#ifdef SYS_mlockall // kernel entry: num=151 func=sys_mlockall
INTERPOSE(mlockall);
#endif
#ifdef SYS_mmap // kernel entry: num=9 func=sys_mmap
INTERPOSE(mmap);
#endif
#ifdef SYS_modify_ldt // kernel entry: num=154 func=sys_modify_ldt
INTERPOSE(modify_ldt);
#endif
#ifdef SYS_mount // kernel entry: num=165 func=sys_mount
INTERPOSE(mount);
#endif
// Skipping SYS_mount_setattr
// Skipping SYS_move_mount
#ifdef SYS_move_pages // kernel entry: num=279 func=sys_move_pages
INTERPOSE(move_pages);
#endif
#ifdef SYS_mprotect // kernel entry: num=10 func=sys_mprotect
INTERPOSE(mprotect);
#endif
#ifdef SYS_mq_getsetattr // kernel entry: num=245 func=sys_mq_getsetattr
INTERPOSE(mq_getsetattr);
#endif
// Skipping SYS_mq_notify
// Skipping SYS_mq_open
#ifdef SYS_mq_timedreceive // kernel entry: num=243 func=sys_mq_timedreceive
INTERPOSE(mq_timedreceive);
#endif
#ifdef SYS_mq_timedsend // kernel entry: num=242 func=sys_mq_timedsend
INTERPOSE(mq_timedsend);
#endif
#ifdef SYS_mq_unlink // kernel entry: num=241 func=sys_mq_unlink
INTERPOSE(mq_unlink);
#endif
#ifdef SYS_mremap // kernel entry: num=25 func=sys_mremap
INTERPOSE(mremap);
#endif
#ifdef SYS_mseal // kernel entry: num=462 func=sys_mseal
INTERPOSE(mseal);
#endif
#ifdef SYS_msgctl // kernel entry: num=71 func=sys_msgctl
INTERPOSE(msgctl);
#endif
#ifdef SYS_msgget // kernel entry: num=68 func=sys_msgget
INTERPOSE(msgget);
#endif
#ifdef SYS_msgrcv // kernel entry: num=70 func=sys_msgrcv
INTERPOSE(msgrcv);
#endif
#ifdef SYS_msgsnd // kernel entry: num=69 func=sys_msgsnd
INTERPOSE(msgsnd);
#endif
#ifdef SYS_msync // kernel entry: num=26 func=sys_msync
INTERPOSE(msync);
#endif
#ifdef SYS_munlock // kernel entry: num=150 func=sys_munlock
INTERPOSE(munlock);
#endif
#ifdef SYS_munlockall // kernel entry: num=152 func=sys_munlockall
INTERPOSE(munlockall);
#endif
#ifdef SYS_munmap // kernel entry: num=11 func=sys_munmap
INTERPOSE(munmap);
#endif
#ifdef SYS_name_to_handle_at // kernel entry: num=303 func=sys_name_to_handle_at
INTERPOSE(name_to_handle_at);
#endif
#ifdef SYS_nanosleep // kernel entry: num=35 func=sys_nanosleep
INTERPOSE(nanosleep);
#endif
// Skipping SYS_newfstatat
// Skipping SYS_open
#ifdef SYS_open_by_handle_at // kernel entry: num=304 func=sys_open_by_handle_at
INTERPOSE(open_by_handle_at);
#endif
// Skipping SYS_open_tree
#ifdef SYS_open_tree_attr // kernel entry: num=467 func=sys_open_tree_attr
INTERPOSE(open_tree_attr);
#endif
// Skipping SYS_openat
#ifdef SYS_openat2 // kernel entry: num=437 func=sys_openat2
INTERPOSE(openat2);
#endif
#ifdef SYS_pause // kernel entry: num=34 func=sys_pause
INTERPOSE(pause);
#endif
#ifdef SYS_perf_event_open // kernel entry: num=298 func=sys_perf_event_open
INTERPOSE(perf_event_open);
#endif
#ifdef SYS_personality // kernel entry: num=135 func=sys_personality
INTERPOSE(personality);
#endif
#ifdef SYS_pidfd_getfd // kernel entry: num=438 func=sys_pidfd_getfd
INTERPOSE(pidfd_getfd);
#endif
#ifdef SYS_pidfd_open // kernel entry: num=434 func=sys_pidfd_open
INTERPOSE(pidfd_open);
#endif
#ifdef SYS_pidfd_send_signal // kernel entry: num=424 func=sys_pidfd_send_signal
INTERPOSE(pidfd_send_signal);
#endif
#ifdef SYS_pipe // kernel entry: num=22 func=sys_pipe
INTERPOSE(pipe);
#endif
#ifdef SYS_pipe2 // kernel entry: num=293 func=sys_pipe2
INTERPOSE(pipe2);
#endif
#ifdef SYS_pivot_root // kernel entry: num=155 func=sys_pivot_root
INTERPOSE(pivot_root);
#endif
#ifdef SYS_pkey_alloc // kernel entry: num=330 func=sys_pkey_alloc
INTERPOSE(pkey_alloc);
#endif
#ifdef SYS_pkey_free // kernel entry: num=331 func=sys_pkey_free
INTERPOSE(pkey_free);
#endif
#ifdef SYS_pkey_mprotect // kernel entry: num=329 func=sys_pkey_mprotect
INTERPOSE(pkey_mprotect);
#endif
// Skipping SYS_poll
// Skipping SYS_ppoll
#ifdef SYS_prctl // kernel entry: num=157 func=sys_prctl
INTERPOSE(prctl);
#endif
// Skipping SYS_pread64
// Skipping SYS_preadv
// Skipping SYS_preadv2
#ifdef SYS_prlimit64 // kernel entry: num=302 func=sys_prlimit64
INTERPOSE(prlimit64);
#endif
// Skipping SYS_process_madvise
// Skipping SYS_process_mrelease
#ifdef SYS_process_vm_readv // kernel entry: num=310 func=sys_process_vm_readv
INTERPOSE(process_vm_readv);
#endif
#ifdef SYS_process_vm_writev // kernel entry: num=311 func=sys_process_vm_writev
INTERPOSE(process_vm_writev);
#endif
// Skipping SYS_pselect6
// Skipping SYS_ptrace
// Skipping SYS_pwrite64
// Skipping SYS_pwritev
// Skipping SYS_pwritev2
#ifdef SYS_quotactl // kernel entry: num=179 func=sys_quotactl
INTERPOSE(quotactl);
#endif
// Skipping SYS_quotactl_fd
#ifdef SYS_read // kernel entry: num=0 func=sys_read
INTERPOSE(read);
#endif
#ifdef SYS_readahead // kernel entry: num=187 func=sys_readahead
INTERPOSE(readahead);
#endif
#ifdef SYS_readlink // kernel entry: num=89 func=sys_readlink
INTERPOSE(readlink);
#endif
#ifdef SYS_readlinkat // kernel entry: num=267 func=sys_readlinkat
INTERPOSE(readlinkat);
#endif
#ifdef SYS_readv // kernel entry: num=19 func=sys_readv
INTERPOSE(readv);
#endif
#ifdef SYS_reboot // kernel entry: num=169 func=sys_reboot
INTERPOSE(reboot);
#endif
#ifdef SYS_recvfrom // kernel entry: num=45 func=sys_recvfrom
INTERPOSE(recvfrom);
#endif
#ifdef SYS_recvmmsg // kernel entry: num=299 func=sys_recvmmsg
INTERPOSE(recvmmsg);
#endif
#ifdef SYS_recvmsg // kernel entry: num=47 func=sys_recvmsg
INTERPOSE(recvmsg);
#endif
#ifdef SYS_remap_file_pages // kernel entry: num=216 func=sys_remap_file_pages
INTERPOSE(remap_file_pages);
#endif
#ifdef SYS_removexattr // kernel entry: num=197 func=sys_removexattr
INTERPOSE(removexattr);
#endif
#ifdef SYS_removexattrat // kernel entry: num=466 func=sys_removexattrat
INTERPOSE(removexattrat);
#endif
#ifdef SYS_rename // kernel entry: num=82 func=sys_rename
INTERPOSE(rename);
#endif
#ifdef SYS_renameat // kernel entry: num=264 func=sys_renameat
INTERPOSE(renameat);
#endif
#ifdef SYS_renameat2 // kernel entry: num=316 func=sys_renameat2
INTERPOSE(renameat2);
#endif
#ifdef SYS_request_key // kernel entry: num=249 func=sys_request_key
INTERPOSE(request_key);
#endif
// Skipping SYS_restart_syscall
#ifdef SYS_rmdir // kernel entry: num=84 func=sys_rmdir
INTERPOSE(rmdir);
#endif
// Skipping SYS_rseq
// Skipping SYS_rt_sigaction
// Skipping SYS_rt_sigpending
// Skipping SYS_rt_sigprocmask
#ifdef SYS_rt_sigqueueinfo // kernel entry: num=129 func=sys_rt_sigqueueinfo
INTERPOSE(rt_sigqueueinfo);
#endif
// Skipping SYS_rt_sigreturn
// Skipping SYS_rt_sigsuspend
// Skipping SYS_rt_sigtimedwait
#ifdef SYS_rt_tgsigqueueinfo // kernel entry: num=297 func=sys_rt_tgsigqueueinfo
INTERPOSE(rt_tgsigqueueinfo);
#endif
#ifdef SYS_sched_get_priority_max // kernel entry: num=146 func=sys_sched_get_priority_max
INTERPOSE(sched_get_priority_max);
#endif
#ifdef SYS_sched_get_priority_min // kernel entry: num=147 func=sys_sched_get_priority_min
INTERPOSE(sched_get_priority_min);
#endif
// Skipping SYS_sched_getaffinity
#ifdef SYS_sched_getattr // kernel entry: num=315 func=sys_sched_getattr
INTERPOSE(sched_getattr);
#endif
#ifdef SYS_sched_getparam // kernel entry: num=143 func=sys_sched_getparam
INTERPOSE(sched_getparam);
#endif
#ifdef SYS_sched_getscheduler // kernel entry: num=145 func=sys_sched_getscheduler
INTERPOSE(sched_getscheduler);
#endif
#ifdef SYS_sched_rr_get_interval // kernel entry: num=148 func=sys_sched_rr_get_interval
INTERPOSE(sched_rr_get_interval);
#endif
// Skipping SYS_sched_setaffinity
#ifdef SYS_sched_setattr // kernel entry: num=314 func=sys_sched_setattr
INTERPOSE(sched_setattr);
#endif
#ifdef SYS_sched_setparam // kernel entry: num=142 func=sys_sched_setparam
INTERPOSE(sched_setparam);
#endif
#ifdef SYS_sched_setscheduler // kernel entry: num=144 func=sys_sched_setscheduler
INTERPOSE(sched_setscheduler);
#endif
#ifdef SYS_sched_yield // kernel entry: num=24 func=sys_sched_yield
INTERPOSE(sched_yield);
#endif
#ifdef SYS_seccomp // kernel entry: num=317 func=sys_seccomp
INTERPOSE(seccomp);
#endif
// Skipping SYS_select
#ifdef SYS_semctl // kernel entry: num=66 func=sys_semctl
INTERPOSE(semctl);
#endif
#ifdef SYS_semget // kernel entry: num=64 func=sys_semget
INTERPOSE(semget);
#endif
#ifdef SYS_semop // kernel entry: num=65 func=sys_semop
INTERPOSE(semop);
#endif
#ifdef SYS_semtimedop // kernel entry: num=220 func=sys_semtimedop
INTERPOSE(semtimedop);
#endif
#ifdef SYS_sendfile // kernel entry: num=40 func=sys_sendfile64
INTERPOSE(sendfile);
#endif
#ifdef SYS_sendmmsg // kernel entry: num=307 func=sys_sendmmsg
INTERPOSE(sendmmsg);
#endif
#ifdef SYS_sendmsg // kernel entry: num=46 func=sys_sendmsg
INTERPOSE(sendmsg);
#endif
#ifdef SYS_sendto // kernel entry: num=44 func=sys_sendto
INTERPOSE(sendto);
#endif
#ifdef SYS_set_mempolicy // kernel entry: num=238 func=sys_set_mempolicy
INTERPOSE(set_mempolicy);
#endif
// Skipping SYS_set_mempolicy_home_node
#ifdef SYS_set_robust_list // kernel entry: num=273 func=sys_set_robust_list
INTERPOSE(set_robust_list);
#endif
#ifdef SYS_set_tid_address // kernel entry: num=218 func=sys_set_tid_address
INTERPOSE(set_tid_address);
#endif
#ifdef SYS_setdomainname // kernel entry: num=171 func=sys_setdomainname
INTERPOSE(setdomainname);
#endif
// Skipping SYS_setfsgid
// Skipping SYS_setfsuid
// Skipping SYS_setgid
// Skipping SYS_setgroups
// Skipping SYS_sethostname
#ifdef SYS_setitimer // kernel entry: num=38 func=sys_setitimer
INTERPOSE(setitimer);
#endif
#ifdef SYS_setns // kernel entry: num=308 func=sys_setns
INTERPOSE(setns);
#endif
#ifdef SYS_setpgid // kernel entry: num=109 func=sys_setpgid
INTERPOSE(setpgid);
#endif
// Skipping SYS_setpriority
// Skipping SYS_setregid
// Skipping SYS_setresgid
// Skipping SYS_setresuid
// Skipping SYS_setreuid
// Skipping SYS_setrlimit
#ifdef SYS_setsid // kernel entry: num=112 func=sys_setsid
INTERPOSE(setsid);
#endif
#ifdef SYS_setsockopt // kernel entry: num=54 func=sys_setsockopt
INTERPOSE(setsockopt);
#endif
#ifdef SYS_settimeofday // kernel entry: num=164 func=sys_settimeofday
INTERPOSE(settimeofday);
#endif
// Skipping SYS_setuid
#ifdef SYS_setxattr // kernel entry: num=188 func=sys_setxattr
INTERPOSE(setxattr);
#endif
#ifdef SYS_setxattrat // kernel entry: num=463 func=sys_setxattrat
INTERPOSE(setxattrat);
#endif
#ifdef SYS_shmat // kernel entry: num=30 func=sys_shmat
INTERPOSE(shmat);
#endif
#ifdef SYS_shmctl // kernel entry: num=31 func=sys_shmctl
INTERPOSE(shmctl);
#endif
#ifdef SYS_shmdt // kernel entry: num=67 func=sys_shmdt
INTERPOSE(shmdt);
#endif
#ifdef SYS_shmget // kernel entry: num=29 func=sys_shmget
INTERPOSE(shmget);
#endif
#ifdef SYS_shutdown // kernel entry: num=48 func=sys_shutdown
INTERPOSE(shutdown);
#endif
#ifdef SYS_sigaltstack // kernel entry: num=131 func=sys_sigaltstack
INTERPOSE(sigaltstack);
#endif
// Skipping SYS_signalfd
// Skipping SYS_signalfd4
#ifdef SYS_socket // kernel entry: num=41 func=sys_socket
INTERPOSE(socket);
#endif
#ifdef SYS_socketpair // kernel entry: num=53 func=sys_socketpair
INTERPOSE(socketpair);
#endif
#ifdef SYS_splice // kernel entry: num=275 func=sys_splice
INTERPOSE(splice);
#endif
#ifdef SYS_stat // kernel entry: num=4 func=sys_newstat
INTERPOSE(stat);
#endif
#ifdef SYS_statfs // kernel entry: num=137 func=sys_statfs
INTERPOSE(statfs);
#endif
#ifdef SYS_statmount // kernel entry: num=457 func=sys_statmount
INTERPOSE(statmount);
#endif
#ifdef SYS_statx // kernel entry: num=332 func=sys_statx
INTERPOSE(statx);
#endif
#ifdef SYS_swapoff // kernel entry: num=168 func=sys_swapoff
INTERPOSE(swapoff);
#endif
#ifdef SYS_swapon // kernel entry: num=167 func=sys_swapon
INTERPOSE(swapon);
#endif
#ifdef SYS_symlink // kernel entry: num=88 func=sys_symlink
INTERPOSE(symlink);
#endif
#ifdef SYS_symlinkat // kernel entry: num=266 func=sys_symlinkat
INTERPOSE(symlinkat);
#endif
#ifdef SYS_sync // kernel entry: num=162 func=sys_sync
INTERPOSE(sync);
#endif
#ifdef SYS_sync_file_range // kernel entry: num=277 func=sys_sync_file_range
INTERPOSE(sync_file_range);
#endif
#ifdef SYS_syncfs // kernel entry: num=306 func=sys_syncfs
INTERPOSE(syncfs);
#endif
#ifdef SYS_sysfs // kernel entry: num=139 func=sys_sysfs
INTERPOSE(sysfs);
#endif
#ifdef SYS_sysinfo // kernel entry: num=99 func=sys_sysinfo
INTERPOSE(sysinfo);
#endif
#ifdef SYS_syslog // kernel entry: num=103 func=sys_syslog
INTERPOSE(syslog);
#endif
#ifdef SYS_tee // kernel entry: num=276 func=sys_tee
INTERPOSE(tee);
#endif
#ifdef SYS_tgkill // kernel entry: num=234 func=sys_tgkill
INTERPOSE(tgkill);
#endif
#ifdef SYS_time // kernel entry: num=201 func=sys_time
INTERPOSE(time);
#endif
// Skipping SYS_timer_create
#ifdef SYS_timer_delete // kernel entry: num=226 func=sys_timer_delete
INTERPOSE(timer_delete);
#endif
#ifdef SYS_timer_getoverrun // kernel entry: num=225 func=sys_timer_getoverrun
INTERPOSE(timer_getoverrun);
#endif
#ifdef SYS_timer_gettime // kernel entry: num=224 func=sys_timer_gettime
INTERPOSE(timer_gettime);
#endif
#ifdef SYS_timer_settime // kernel entry: num=223 func=sys_timer_settime
INTERPOSE(timer_settime);
#endif
#ifdef SYS_timerfd_create // kernel entry: num=283 func=sys_timerfd_create
INTERPOSE(timerfd_create);
#endif
#ifdef SYS_timerfd_gettime // kernel entry: num=287 func=sys_timerfd_gettime
INTERPOSE(timerfd_gettime);
#endif
#ifdef SYS_timerfd_settime // kernel entry: num=286 func=sys_timerfd_settime
INTERPOSE(timerfd_settime);
#endif
#ifdef SYS_times // kernel entry: num=100 func=sys_times
INTERPOSE(times);
#endif
#ifdef SYS_tkill // kernel entry: num=200 func=sys_tkill
INTERPOSE(tkill);
#endif
#ifdef SYS_truncate // kernel entry: num=76 func=sys_truncate
INTERPOSE(truncate);
#endif
#ifdef SYS_umask // kernel entry: num=95 func=sys_umask
INTERPOSE(umask);
#endif
#ifdef SYS_umount2 // kernel entry: num=166 func=sys_umount
INTERPOSE(umount2);
#endif
#ifdef SYS_uname // kernel entry: num=63 func=sys_newuname
INTERPOSE(uname);
#endif
#ifdef SYS_unlink // kernel entry: num=87 func=sys_unlink
INTERPOSE(unlink);
#endif
#ifdef SYS_unlinkat // kernel entry: num=263 func=sys_unlinkat
INTERPOSE(unlinkat);
#endif
#ifdef SYS_unshare // kernel entry: num=272 func=sys_unshare
INTERPOSE(unshare);
#endif
#ifdef SYS_uprobe // kernel entry: num=336 func=sys_uprobe
INTERPOSE(uprobe);
#endif
#ifdef SYS_uretprobe // kernel entry: num=335 func=sys_uretprobe
INTERPOSE(uretprobe);
#endif
#ifdef SYS_userfaultfd // kernel entry: num=323 func=sys_userfaultfd
INTERPOSE(userfaultfd);
#endif
#ifdef SYS_ustat // kernel entry: num=136 func=sys_ustat
INTERPOSE(ustat);
#endif
#ifdef SYS_utime // kernel entry: num=132 func=sys_utime
INTERPOSE(utime);
#endif
#ifdef SYS_utimensat // kernel entry: num=280 func=sys_utimensat
INTERPOSE(utimensat);
#endif
#ifdef SYS_utimes // kernel entry: num=235 func=sys_utimes
INTERPOSE(utimes);
#endif
// Skipping SYS_vfork
#ifdef SYS_vhangup // kernel entry: num=153 func=sys_vhangup
INTERPOSE(vhangup);
#endif
#ifdef SYS_vmsplice // kernel entry: num=278 func=sys_vmsplice
INTERPOSE(vmsplice);
#endif
// Skipping SYS_wait4
// Skipping SYS_waitid
#ifdef SYS_write // kernel entry: num=1 func=sys_write
INTERPOSE(write);
#endif
#ifdef SYS_writev // kernel entry: num=20 func=sys_writev
INTERPOSE(writev);
#endif
// clang-format on
