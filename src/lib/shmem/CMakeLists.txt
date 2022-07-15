set(SHD_SHMEM_SRC
    buddy.c
    shmem_allocator.c
    shmem_file.c
    shmem_util.c)

include_directories(${GLIB_INCLUDES})

add_library(shadow-shmem STATIC ${SHD_SHMEM_SRC})

# -fPIC enables linking the static compile unit with a shared object library
target_compile_options(shadow-shmem PRIVATE -fPIC)

target_link_libraries(shadow-shmem INTERFACE logger)

add_executable(shd-shmem-test shmem_test.c)
target_link_libraries(shd-shmem-test shadow-shmem
    ${GLIB_LIBRARIES} ${RT_LIBRARIES} ${M_LIBRARIES})
add_test(NAME shmem COMMAND shd-shmem-test)
