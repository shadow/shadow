## for now, interposer includes shadow.h, so it needs to know about glib and igraph

#add_cflags(-Wno-invalid-noreturn)

## build the required library for intercepting system and library calls.
## create the interpose library, to be set as LD_PRELOAD to intercept functions.
## the functions we intercept MUST be in a shared library for dlsym searching.
## we redirect them to our internal shadow versions, or to libc.
# include_directories(${CMAKE_SOURCE_DIR}/src/main)

add_cflags(-fPIC)

set(SHIM_LIB shadow-shim)
set(SHIM_FILES
  preload.c
  shim.c
  socket.c
)
add_library(${SHIM_LIB} SHARED ${SHIM_FILES})
set_target_properties(${SHIM_LIB} PROPERTIES 
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib"
)
target_compile_options(${SHIM_LIB} PRIVATE -pthread -DSHADOW_SHIM)
target_link_libraries(${SHIM_LIB} -pthread -ldl)
install(TARGETS ${SHIM_LIB} DESTINATION lib)

