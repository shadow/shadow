## we will use cmake's external module feature to build our external projects
## http://www.kitware.com/media/html/BuildingExternalProjectsWithCMake2.8.html
INCLUDE(ExternalProject)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RPTH_DEBUG_SWITCH "--enable-debug=yes")
    set(RPTH_OPT_SWITCH "--enable-optimize=no")
else()
    set(RPTH_DEBUG_SWITCH "--enable-debug=no")
    set(RPTH_OPT_SWITCH "--enable-optimize=yes")
endif()

if($ENV{VERBOSE})
    set(RPTH_VERB_SWITCH "--verbose")
else()
    set(RPTH_VERB_SWITCH "--quiet")
endif()

EXTERNALPROJECT_ADD(
    "rpth"
    PREFIX rpth
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rpth
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/rpth
    CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rpth/configure ${RPTH_VERB_SWITCH} --prefix=${CMAKE_BINARY_DIR} --with-tags= --disable-shared --disable-tests ${RPTH_DEBUG_SWITCH} ${RPTH_OPT_SWITCH}
    CFLAGS=-Qunused-arguments
    BUILD_COMMAND make
    BUILD_IN_SOURCE 0
    INSTALL_COMMAND ""
)

# trying to make sure the external project gets rebuilt when a rpth src file changes.
# i dont think this works yet...
add_custom_target(rpth-src DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_debug.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_ring.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_pqueue.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_time.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_errno.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_mctx.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_uctx.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_tcb.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_sched.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_attr.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_lib.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_event.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_data.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_clean.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_cancel.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_msg.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_sync.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_fork.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_util.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_high.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_syscall.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_ext.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_compat.c ${CMAKE_CURRENT_SOURCE_DIR}/rpth/pth_string.c)
add_dependencies(rpth rpth-src)

#ADD_LIBRARY(rpth-imp STATIC IMPORTED)
#SET_TARGET_PROPERTIES(rpth PROPERTIES
#    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/rpth/rpth${CMAKE_STATIC_LIBRARY_SUFFIX}"
#    IMPORTED_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/rpth/librpth.la"
#)
