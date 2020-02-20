######################################################################################################
## ADD_SHADOW_PLUGIN                                                                                ##
######################################################################################################

## the "ususal" way to build plug-ins, i.e., as shared libraries
## the main benefit of a shared library is that the internal symbols can be interposed with LD_PRELOAD
## so, this should be used for plugins that want to interpose their internal symbols
## (also, the .so extension may make it more obvious this is meant to be loaded by shadow)
macro(add_shadow_plugin target)
    add_library(${target} SHARED ${ARGN})
    set_target_properties(${target} PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-rdynamic -Wl,--no-as-needed"
    )
endmacro(add_shadow_plugin)

######################################################################################################
## ADD_SHADOW_EXE                                                                                   ##
######################################################################################################

## an alternative to the shared library is to build a position-independent executable as follows
## the main benefit is that the executable produced can be run outside of shadow as usual
## this can still be loaded by shadow, but the internal symbols will not be interposed by LD_PRELOAD
## so, this should be used for plugins that dont need or want their internal symbols interposed
macro(add_shadow_exe target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed"
    )
endmacro(add_shadow_exe)

######################################################################################################
## ADD_CFLAGS                                                                                       ##
######################################################################################################

macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

######################################################################################################
## ADD_LDFLAGS                                                                                       ##
######################################################################################################

macro(add_ldflags)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${ARGN}")
endmacro(add_ldflags)

######################################################################################################
