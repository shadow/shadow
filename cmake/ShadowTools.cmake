######################################################################################################
## ADD_CFLAGS                                                                                       ##
######################################################################################################

macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

######################################################################################################
## ADD_LDFLAGS                                                                                      ##
######################################################################################################

macro(add_ldflags)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ARGN}")
    # We do *not* add to CMAKE_STATIC_LINKER_FLAGS, since that is used when "linking" a static
    # library with `ar`, not when statically linking an executable.
endmacro(add_ldflags)
