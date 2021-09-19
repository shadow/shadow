add_library(shadow_syscalls SHARED syscalls.c)
target_compile_options(shadow_syscalls PRIVATE -D_GNU_SOURCE -fPIC)
install(TARGETS shadow_syscalls DESTINATION lib)
