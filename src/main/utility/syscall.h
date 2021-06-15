#ifndef MAIN_UTILITY_SYSCALL_H
#define MAIN_UTILITY_SYSCALL_H

// Returns what the `errno` value would be for the given syscall instruction result.
static inline int syscall_rawReturnValueToErrno(long rv) {
    if (rv <= -1 && rv >= -4095) {
        // Linux reserves -1 through -4095 for errors. See
        // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
        return -rv;
    }
    return 0;
}

#endif
