project(elf-loader C ASM)

message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

if( ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" )
    set( BLA_LIBRARY "/opt/lib/libBla.so" )
else()
    set( BLA_LIBRARY "/opt/lib32/libBla.so" )
endif()

add_definitions(-DMALLOC_DEBUG_ENABLE)
add_definitions(-D_GNU_SOURCE)
add_definitions(-DLDSO_SONAME=\"ldso\")

add_cflags(-g3)
add_cflags(-Wall)
add_cflags(-Werror)
add_cflags(-Wall)
add_cflags(-fno-stack-protector)
add_cflags(-fpic)
add_cflags(-fvisibility=hidden)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/x86_64)

set(elf_loader_as
        x86_64/stage0.S
        x86_64/machine.c
        x86_64/resolv.S
)

set(elf_loader_srcs
        alloc.c 
        avprintf-cb.c 
#        display-relocs.c 
        dprintf.c 
#        elfedit.c 
        futex.c 
        gdb.c 
        glibc.c 
        interp.c 
#        libvdl.c 
#        readversiondef.c 
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

#add_executable(readversiondef readversiondef.c)
add_executable(ldso ${elf-loader-srcs} ${elf_loader_as})
