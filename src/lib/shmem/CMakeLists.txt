include_directories(${GLIB_INCLUDE_DIRS})
add_executable(shd-shmem-test shmem_test.c)
target_link_libraries(shd-shmem-test shadow-shmem logger
    ${GLIB_LIBRARIES} ${RT_LIBRARIES} ${M_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
add_test(NAME shmem COMMAND shd-shmem-test)
