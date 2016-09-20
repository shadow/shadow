## we will use cmake's external module feature to build our external projects
## http://www.kitware.com/media/html/BuildingExternalProjectsWithCMake2.8.html
INCLUDE(ExternalProject)

##### rpth #####

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

##### elf-loader #####

#add_subdirectory(elf-loader)

EXTERNALPROJECT_ADD(
    "elf-loader"
    PREFIX elf-loader
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader
#    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/elf-loader
    CONFIGURE_COMMAND make vdl-config.h
    CFLAGS=
    BUILD_COMMAND make
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install PREFIX=${CMAKE_CURRENT_BINARY_DIR}/elf-loader
#    TEST_COMMAND make test
#    TEST_BEFORE_INSTALL 1
)

add_custom_target(elf-loader-src DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/alloc.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/avprintf-cb.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/display-relocs.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/dprintf.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/elfedit.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/futex.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/gdb.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/glibc.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/interp.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/libvdl.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/readversiondef.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/stage1.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/stage2.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/system.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/valgrind.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-alloc.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-context.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-dl.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-dl-public.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-fini.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-gc.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-init.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-linkmap.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-list.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-log.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-lookup.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-map.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-mem.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-reloc.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-sort.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-tls.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-unmap.c ${CMAKE_CURRENT_SOURCE_DIR}/elf-loader/vdl-utils.c)
add_dependencies(elf-loader elf-loader-src)

#ADD_LIBRARY(elf-loader-ldso STATIC IMPORTED)
#SET_TARGET_PROPERTIES(elf-loader-ldso PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/ldso")
add_custom_target(elf-loader-ldso DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/ldso)
add_dependencies(elf-loader-ldso elf-loader)

#ADD_LIBRARY(elf-loader-vdl SHARED IMPORTED)
#SET_TARGET_PROPERTIES(elf-loader-vdl PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/libvdl.so")
add_custom_target(elf-loader-vdl DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/libvdl.so)
add_dependencies(elf-loader-vdl elf-loader)

## using PROGRAMS here to make sure the executable bit is set
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/ldso ${CMAKE_CURRENT_BINARY_DIR}/elf-loader/lib/libvdl.so DESTINATION lib/)

#install(TARGETS elf-loader-libvdl DESTINATION lib)
#install(TARGETS elf-loader-ldso DESTINATION lib)
#install(TARGETS elf-loader-elfedit DESTINATION bin)