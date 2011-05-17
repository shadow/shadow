## the functions we intercept MUST be in a shared library for dlsym searching
add_library(shadow-intercept SHARED vsocket_intercept.c vsystem_intercept.c vevent_intercept.c vcrypto_intercept.c)

## create the preload library, will be set as LD_PRELOAD to intercept functions
## and redirect them to our intercept library above
add_library(shadow-preload SHARED socket_preload.c system_preload.c event_preload.c crypto_preload.c)

## link with dependency
target_link_libraries(shadow-preload dl)