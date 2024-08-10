import urllib.request
import subprocess

'''
This script is meant to generate our interpose list containing all x86_64
syscalls that are defined in the Linux kernel. The idea is that we can preload
all syscalls even if Shadow doesn't yet support them all. Preloading is faster
than using seccomp to interpose, so we should use preloading whenever possible.
'''

# Defines the latest syscalls.
# See also https://github.com/torvalds/linux/tree/master/arch/x86/entry/syscalls
syscall_tbl = 'https://raw.githubusercontent.com/torvalds/linux/master/arch/x86/entry/syscalls/syscall_64.tbl'

# libc wrappers which use a different syscall
remap = {}
remap['eventfd'] = 'eventfd2' # libc eventfd() calls SYS_eventfd2

# syscalls we should not generate C wrappers for
skip = set()

# glibc doesn't have these wrappers
skip.update([
    'close_range',
    'eventfd2',
    'fadvise64',
    'futex_waitv',
    'memfd_secret',
    'newfstatat',
    'getdents',
    'getdents64',
    'kcmp',
    'kexec_file_load',
    'open_tree',
    'pread64',
    'pselect6',
    'pwrite64',
    'restart_syscall',
    'rseq',
    'rt_sigaction',
    'rt_sigprocmask',
    'rt_sigpending',
    'rt_sigreturn',
    'rt_sigsuspend',
    'rt_sigtimedwait',
])

# Manually implemented in libc_impls.c
skip.update([
    'open',
    'openat',
])

# Confirmed C library/kernel differences
skip.update([
    'brk',
    'chmod',
    'clone',
    'clone3',
    'epoll_pwait',
    'epoll_pwait2',
    'faccessat',
    'faccessat2',
    'fork',
    'exit',
    'getcwd',
    'mq_notify',
    'mq_open',
    'poll',
    'ppoll',
    'preadv',
    'preadv2',
    'pwritev',
    'pwritev2',
    'sched_getaffinity',
    'sched_setaffinity',
    'select',
    'setuid',
    'sigaction',
    'sigprocmask',
    'signalfd',
    'signalfd4',
    'timer_create',
    'vfork',
    'wait4',
    'waitid',
])

# man page has "C library/kernel differences" or was missing. Skip pending review.
skip.update([
    'fchmod',
    'fchmodat',
    'fchmodat2',
    'fsconfig',
    'fsmount',
    'fsopen',
    'fspick',
    'getgroups',
    'getpriority',
    'io_pgetevents',
    'io_uring_enter',
    'io_uring_register',
    'io_uring_setup',
    'landlock_add_rule',
    'landlock_create_ruleset',
    'landlock_restrict_self',
    'mount_setattr',
    'move_mount',
    'process_madvise',
    'process_mrelease',
    'ptrace',
    'quotactl_fd',
    'set_mempolicy_home_node',
    'setfsgid',
    'setfsuid',
    'setgid',
    'setgroups',
    'sethostname',
    'setpriority',
    'setregid',
    'setresgid',
    'setresuid',
    'setreuid',
])

ignore_differences = set([
    # man page notes that clock_gettime may go through VDSO. That's ok.
    'clock_gettime',
    'clock_getres',
    'clock_settime',

    # man page notes that gettimeofday may go through VDSO. That's ok.
    'gettimeofday',
    'settimeofday',

    # man page notes that it may go through VDSO. That's ok.
    'time',

    # man page notes differences for other syscalls on same page.
    'creat',

    # eventfd uses eventfd2 syscall. We remap this correctly.
    'eventfd',

    # man page notes that epoll_pwait has differences. epoll_wait should be ok.
    'epoll_wait',

    # man page notes that some versions of libc cache the pid. Skipping that
    # behavior shouldn't break anything.
    'getpid',
    'getppid',

    # the glibc wrapper actually uses the mmap2 syscall. Using the mmap
    # syscall shouldn't hurt anything, though.
    'mmap',
    'munmap',

    # man page notes that preadv and pwritev have differences. These ones
    # are on the same page but don't have documented differences.
    'readv',
    'writev',

    # man page notes that faccessat and faccessat2 have differences.
    'access',

    # man page says to *check for* sections called "C library/kernel differences"
    # when intercepting syscalls.
    # Doesn't actually have such a section itself.
    'seccomp',

    # stat(2):
    # > Over  time,  increases  in the size of the stat structure have led to
    # > three successive versions of stat(): sys_stat() (slot __NR_oldstat),
    # > sys_newstat() (slot __NR_stat), and sys_stat64() (slot __NR_stat64) on
    # > 32-bit platforms such as i386.  The first two ver‐ #  sions were already
    # > present in Linux 1.0 (albeit with different names); the last was added in
    # > Linux 2.4.  Similar remarks apply for fstat() and lstat().
    #
    # Since we only support 64 bit systems and relatively new versions of glibc
    # we want the stat library call to map to call syscall number __NR_stat;
    # i.e. the default-generated wrapper should be correct.
    'stat',
    'lstat',
    'fstat',

    # uname(2):
    # > Over  time,  increases  in  the size of the utsname structure have led to
    # > three successive versions of uname(): sys_olduname() (slot
    # > __NR_oldolduname), sys_uname() (slot __NR_olduname), and sys_newuname()
    # > (slot __NR_uname).  The first one used length 9 for all fields; the second
    # > used 65; the third also uses 65 but adds the domainname field.  The glibc
    # > uname() wrapper function hides these details from applications, invoking
    # > the most recent version of the system call provided by the kernel.
    #
    # We want __NR_uname, so the generated wrapper should be correct.
    'uname',
])

# syscall wrappers that return errors directly instead of through errno.
direct_errors = set()
direct_errors.add('clock_nanosleep')

with urllib.request.urlopen(syscall_tbl) as response:
    data = response.read().decode("utf-8")

syscalls = {}

for line in data.splitlines():
    parts = line.split()

    # ignore comments and incomplete lines
    if len(parts) < 4 or '#' in parts[0]:
        continue

    num, abi, name, entry = parts

    # ignore the x32-specific abi, since shadow only supports x86_64
    if 'x32' in abi:
        continue

    syscalls[str(name)] = [num, str(entry)]

header = \
'''/// This file is generated with the 'gen_syscall_wrappers_c.py' script and in
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
'''

with open('syscall_wrappers.c', 'w') as outf:
    print(header, file=outf)

    print('// clang-format off', file=outf)

    for name in sorted(syscalls.keys()):
        num, entry = syscalls[name]
        if name in skip:
            print(f'// Skipping SYS_{name}', file=outf)
            continue
        if name not in ignore_differences:
            try:
                man = subprocess.check_output(["man", "2", name], encoding="utf8", stderr=subprocess.DEVNULL)
            except subprocess.CalledProcessError:
                print(f"Warning: SYS_{name}: couldn't find man page")
            if "C library/kernel differences" in man:
                print(f"Warning: SYS_{name}: man page has 'C library/kernel differences'")
        print(f'#ifdef SYS_{name} // kernel entry: num={num} func={entry}', file=outf)
        if name in remap:
            print(f'INTERPOSE_REMAP({name}, {remap[name]});', file=outf)
        elif name in direct_errors:
            print(f'INTERPOSE_DIRECT_ERRORS({name});', file=outf)
        else:
            print(f'INTERPOSE({name});', file=outf)
        print('#endif', file=outf)

    print('// clang-format on', file=outf)
