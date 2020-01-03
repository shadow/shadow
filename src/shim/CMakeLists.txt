## for now, interposer includes shadow.h, so it needs to know about glib and igraph

#add_cflags(-Wno-invalid-noreturn)

## build the required library for intercepting system and library calls.
## create the interpose library, to be set as LD_PRELOAD to intercept functions.
## the functions we intercept MUST be in a shared library for dlsym searching.
## we redirect them to our internal shadow versions, or to libc.
# include_directories(${CMAKE_SOURCE_DIR}/src/main)

set(SHIM_FILES
  shim.c
)

set(SHIM_LIB shadow-shim)

add_library(${SHIM_LIB} SHARED ${SHIM_FILES})

target_compile_options(${SHIM_LIB} PRIVATE -pthread)

set_target_properties(${SHIM_LIB} PROPERTIES 
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib"
)

target_link_libraries(${SHIM_LIB} -pthread)
install(TARGETS ${SHIM_LIB} DESTINATION lib)
