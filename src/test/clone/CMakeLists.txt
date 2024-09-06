include_directories(${GLIB_INCLUDE_DIRS})
add_executable(test_clone_leader_exits_early test_clone_leader_exits_early.c ../test_common.c)
target_compile_options(test_clone_leader_exits_early PUBLIC "-pthread")
target_link_libraries(test_clone_leader_exits_early ${GLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
add_linux_tests(BASENAME clone_leader_exits_early COMMAND test_clone_leader_exits_early)

add_shadow_tests(
    BASENAME clone_leader_exits_early
    # Shim-side strace-logging use libc functions that assume native
    # thread-local-storage is set up. It *usually* works in practice, but is a
    # potential source of hard-to-debug errors.
    #
    # See https://github.com/shadow/shadow/issues/2919
    ARGS --strace-logging-mode=off
)

# This test exercises some corner cases in memory management. Useful to test it
# without the memory manager (really the MemoryMapper) enabled.
add_shadow_tests(
    BASENAME clone_leader_exits_early_nomm
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/clone_leader_exits_early.yaml"
    # Shim-side strace-logging use libc functions that assume native
    # thread-local-storage is set up. It *usually* works in practice, but is a
    # potential source of hard-to-debug errors.
    #
    # See https://github.com/shadow/shadow/issues/2919
    ARGS --strace-logging-mode=off --use-memory-manager=false
)

# FIXME: Currently broken, probably because this test
# doesn't set up thread local storage properly in new threads.
# https://github.com/shadow/shadow/issues/3403
# add_linux_tests(BASENAME clone COMMAND sh -c "../../target/debug/test_clone --libc-passing")

add_shadow_tests(
    BASENAME clone
    # Shim-side strace-logging use libc functions that assume native
    # thread-local-storage is set up. It *usually* works in practice, but is a
    # potential source of hard-to-debug errors.
    #
    # See https://github.com/shadow/shadow/issues/2919
    ARGS --strace-logging-mode=off
)

add_linux_tests(BASENAME fork COMMAND sh -c "../../target/debug/test_fork --libc-passing")
add_shadow_tests(
    BASENAME fork
    # The memory mapper is not currently supported with fork
    ARGS --use-memory-manager=false)
