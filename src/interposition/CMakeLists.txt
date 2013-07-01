## the functions we intercept MUST be in a shared library for dlsym searching
add_library(shadow-intercept SHARED intercept.c)
install(TARGETS shadow-intercept DESTINATION lib)

## create the preload library, will be set as LD_PRELOAD to intercept functions
## and redirect them to our intercept library above
add_library(shadow-preload SHARED preload.c preload-malloc.c)

## link with dependency
target_link_libraries(shadow-preload dl)
install(TARGETS shadow-preload DESTINATION lib)
install(FILES preload.h DESTINATION include/)
