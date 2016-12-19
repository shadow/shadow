## for now, interposer includes shadow.h, so it needs to know about glib and igraph
find_package(GLIB REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(DL REQUIRED)
include_directories(${DL_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

#add_cflags(-Wno-invalid-noreturn)

## build the required library for intercepting system and library calls.
## create the interpose library, to be set as LD_PRELOAD to intercept functions.
## the functions we intercept MUST be in a shared library for dlsym searching.
## we redirect them to our internal shadow versions, or to libc.
include_directories(${CMAKE_SOURCE_DIR}/src/main)

add_library(shadow-interpose SHARED shd-interposer.c shd-preload-functions.c)
add_library(shadow-interpose-helper SHARED shd-interposer-helper.c)

set_target_properties(shadow-interpose PROPERTIES 
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib"
)

target_link_libraries(shadow-interpose ${DL_LIBRARIES})
install(TARGETS shadow-interpose DESTINATION lib)
install(TARGETS shadow-interpose-helper DESTINATION lib)