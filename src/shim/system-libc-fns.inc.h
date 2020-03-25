// When adding a function here, add it also to system-libc.h
SYSTEM_LIBC_FN(abort, void, (void));
SYSTEM_LIBC_FN(recv, ssize_t, (int a, void *b, size_t c, int flags));
SYSTEM_LIBC_FN(send, ssize_t, (int a, const void *b, size_t c, int flags));
SYSTEM_LIBC_FN(syscall, long, (long n, ...))
