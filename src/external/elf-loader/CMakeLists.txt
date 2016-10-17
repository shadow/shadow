project(elf-loader C CXX ASM)

add_definitions(-DMALLOC_DEBUG_ENABLE)
add_definitions(-D_GNU_SOURCE)
add_definitions(-DLDSO_SONAME=\"ldso\")

add_cflags(-g3)
add_cflags(-Wall)
#add_cflags(-Werror)
add_cflags(-fno-stack-protector)
add_cflags(-fPIC)
add_cflags(-fvisibility=hidden)
add_cflags(-MD)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/x86_64)


message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

if( ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" )
    set(as_srcs
        x86_64/stage0.S
        x86_64/machine.c
        x86_64/resolv.S
    )

    if(EXISTS /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2)
        set(LDSO_FILE /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2)
    elseif(EXISTS /lib64/ld-linux-x86-64.so.2)
        set(LDSO_FILE /lib64/ld-linux-x86-64.so.2)
    endif()
    message(STATUS "LDSO = ${LDSO_FILE}")

    if(EXISTS /lib/x86_64-linux-gnu/libdl.so.2)
        set(LIBDL_FILE /lib/x86_64-linux-gnu/libdl.so.2)
    elseif(EXISTS /lib64/libdl.so.2)
        set(LIBDL_FILE /lib64/libdl.so.2)
    endif()
    message(STATUS "LIBDL = ${LIBDL_FILE}")

else()
    set(as_srcs
        i386/stage0.S
        i386/machine.c
        i386/resolv.S
    )

    set(LDSO_FILE /lib/ld-linux.so.2)
    message(STATUS "LDSO = ${LDSO_FILE}")

    if(EXISTS /lib/i386-linux-gnu/libdl.so.2)
        set(LIBDL_FILE /lib/i386-linux-gnu/libdl.so.2)
    elseif(EXISTS /lib/libdl.so.2)
        set(LIBDL_FILE /lib/libdl.so.2)
    endif()
    message(STATUS "LIBDL = ${LIBDL_FILE}")

    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} --32 -march=i386")
    add_cflags(-m32)
    set(CMAKE_LINK_FLAGS "${CMAKE_LINK_FLAGS} -m32")
endif()

set(ldso_srcs
    alloc.c
    avprintf-cb.c
#    display-relocs.c
    dprintf.c
#    elfedit.c
    futex.c
    gdb.c
    glibc.c
    interp.c
#    libvdl.c
#    readversiondef.c
    stage1.c
    stage2.c
    system.c
    valgrind.c
    vdl.c
    vdl-alloc.c
    vdl-context.c
    vdl-dl.c
    vdl-dl-public.c
    vdl-fini.c
    vdl-gc.c
    vdl-init.c
    vdl-linkmap.c
    vdl-list.c
    vdl-log.c
    vdl-lookup.c
    vdl-map.c
    vdl-mem.c
    vdl-reloc.c
    vdl-sort.c
    vdl-tls.c
    vdl-unmap.c
    vdl-utils.c
)


add_executable(readversiondef readversiondef.c)


add_custom_command(OUTPUT vdl-config.h
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/extract-system-config.py --config=vdl-config.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating vdl-config.h"
  VERBATIM
)
add_custom_target(vdlconfig DEPENDS vdl-config.h)


add_custom_command(OUTPUT ldso.version
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/readversiondef ${LDSO_FILE} ldso.version.tmp
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/vdl-dl.version ldso.version.tmp > ldso.version
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS readversiondef ${CMAKE_CURRENT_SOURCE_DIR}/vdl-dl.version
  COMMENT "Generating ldso.version"
  VERBATIM
)
add_custom_target(ldsoversion DEPENDS ldso.version)


add_custom_command(OUTPUT libdl.version
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/readversiondef ${LIBDL_FILE} libdl.version.tmp
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/libvdl.version libdl.version.tmp > libdl.version
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS readversiondef ${CMAKE_CURRENT_SOURCE_DIR}/libvdl.version
  COMMENT "Generating ldso.version"
  VERBATIM
)
add_custom_target(libdlversion DEPENDS libdl.version)


add_library(ldso SHARED ${ldso_srcs} ${as_srcs})
add_dependencies(ldso vdlconfig ldsoversion)
target_link_libraries(ldso -lgcc)
set_target_properties(ldso PROPERTIES 
    LINK_FLAGS "-nostartfiles -nostdlib -Wl,--entry=stage0,--version-script=${CMAKE_CURRENT_BINARY_DIR}/ldso.version,-Bstatic"
    PREFIX ""
    SUFFIX ""
)

add_library(vdl SHARED libvdl.c)
add_dependencies(vdl ldso libdlversion)
target_link_libraries(vdl ldso)
set_target_properties(vdl PROPERTIES 
    LINK_FLAGS "-nostdlib -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/libdl.version"
)


add_executable(elfedit elfedit.c)
add_executable(display-relocs display-relocs.c)


add_custom_target(elf-loader DEPENDS ldso vdl elfedit display-relocs)
install(TARGETS elfedit display-relocs DESTINATION bin)
install(TARGETS ldso vdl 
        DESTINATION lib
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_EXECUTE GROUP_READ
                    WORLD_EXECUTE WORLD_READ
)

## build test if enabled
if(SHADOW_TEST STREQUAL ON)
    enable_testing()

    set(test_srcs
        internal-tests.cc
        internal-test-alloc.cc
        internal-test-futex.cc
        internal-test-list.cc
        alloc.c
        futex.c
        vdl-list.c
    )
    
    add_executable(internal-tests ${test_srcs})
    target_link_libraries(internal-tests -lpthread)
    add_test(NAME test-elfloader-internal COMMAND internal-tests)

    add_subdirectory(test)
endif(SHADOW_TEST STREQUAL ON)
