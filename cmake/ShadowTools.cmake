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
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${ARGN}")
endmacro(add_ldflags)

######################################################################################################
## COMPILE_TEST - helper for testing if a source file compiles correctly in our environment         ##
######################################################################################################

macro(compile_test resultvar srcfile)
    try_compile(${resultvar} "${CMAKE_BINARY_DIR}" "${srcfile}" COMPILE_DEFINITIONS "-D_GNU_SOURCE=1 -c")
    if(${resultvar} STREQUAL "TRUE")
        set(${resultvar} 1)
    else()
        set(${resultvar} 0)
    endif()
    MESSAGE(STATUS "${resultvar} = ${${resultvar}}")
endmacro()

######################################################################################################
