## create the interpose library, will be set as LD_PRELOAD to intercept functions
## the functions we intercept MUST be in a shared library for dlsym searching
## we redirect them to our internal shadow versions, or to libc
add_library(shadow-interpose SHARED shd-interposer.c)
target_link_libraries(shadow-interpose dl)
install(TARGETS shadow-interpose DESTINATION lib)
