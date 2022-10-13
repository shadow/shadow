import urllib.request

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
# libc doesn't have an eventfd2() wrapper
skip.add('eventfd2')
# the offset is split into two arguments
skip.add('preadv')
skip.add('preadv2')
skip.add('pwritev')
skip.add('pwritev2')
# `exit` has a non-trivial wrapper that calls `atexit` hooks, flushes open
# `FILE*` objects, etc.
skip.add('exit')

# These have an optional `mode` argument, which the wrappers should initialize
# to 0 when not provided by the caller.
skip.add('open')
skip.add('openat')

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
        else:
            print(f'#ifdef SYS_{name} // kernel entry: num={num} func={entry}', file=outf)
            if name in remap:
                print(f'INTERPOSE_REMAP({name}, {remap[name]});', file=outf)
            elif name in direct_errors:
                print(f'INTERPOSE_DIRECT_ERRORS({name});', file=outf)
            else:
                print(f'INTERPOSE({name});', file=outf)
            print('#endif', file=outf)

    print('// clang-format on', file=outf)
