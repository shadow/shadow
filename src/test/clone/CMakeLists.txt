include_directories(${GLIB_INCLUDE_DIRS})
add_executable(test_clone test_clone.c ../test_common.c)
target_compile_options(test_clone PUBLIC "-pthread")
target_link_libraries(test_clone ${GLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
add_linux_tests(BASENAME clone COMMAND test_clone)

add_shadow_tests(
    BASENAME clone
    # Shim-side strace-logging use libc functions that assume native
    # thread-local-storage is set up. It *usually* works in practice, but is a
    # potential source of hard-to-debug errors.
    #
    # See https://github.com/shadow/shadow/issues/2919
    ARGS --strace-logging-mode=off
)

# The clone test exercises some corner cases in memory management, particularly
# when the thread leader exits before all the threads. Useful to test it without
# the memory manager (really the MemoryMapper) enabled.
add_shadow_tests(
    BASENAME clone-nomm
    # Shim-side strace-logging use libc functions that assume native
    # thread-local-storage is set up. It *usually* works in practice, but is a
    # potential source of hard-to-debug errors.
    #
    # See https://github.com/shadow/shadow/issues/2919
    ARGS --strace-logging-mode=off
)
