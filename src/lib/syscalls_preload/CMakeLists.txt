# The libc preload library enables us to interpose on syscalls (man section 2)
# and libcalls (man section 3) from libc via preloading so that we can avoid as
# much as possible interposing via the seccomp method which is slightly less
# efficient. Preloading means we can prevent mode transitions and executions of
# the seccomp filter.
add_library(shadow_syscalls SHARED libcalls.c syscalls.c)
# Disable the builtin execve, exit, and fork functions in this compilation unit
# which have different call signatures than the functions in syscalls.c.
target_compile_options(shadow_syscalls PRIVATE -D_GNU_SOURCE -fPIC 
    -fno-builtin-execve -fno-builtin-exit -fno-builtin-fork)
install(TARGETS shadow_syscalls DESTINATION lib)
