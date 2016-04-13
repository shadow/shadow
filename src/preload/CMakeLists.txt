## for now, interposer includes shadow.h, so it needs to know about glib and igraph
find_package(GLIB REQUIRED)
find_package(IGRAPH REQUIRED)
include_directories(${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

add_cflags(-Wno-invalid-noreturn)

## build the required library for intercepting system and library calls.
## create the interpose library, to be set as LD_PRELOAD to intercept functions.
## the functions we intercept MUST be in a shared library for dlsym searching.
## we redirect them to our internal shadow versions, or to libc.
include_directories(${CMAKE_SOURCE_DIR}/src/main)
add_library(shadow-interpose SHARED shd-interposer.c)
target_link_libraries(shadow-interpose dl)
install(TARGETS shadow-interpose DESTINATION lib)
