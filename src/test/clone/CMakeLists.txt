include_directories(${GLIB_INCLUDE_DIRS})
add_executable(test_clone test_clone.c ../test_common.c)
target_compile_options(test_clone PUBLIC "-pthread")
target_link_libraries(test_clone ${GLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
add_linux_tests(BASENAME clone COMMAND test_clone)

# FIXME: This test doesn't implement native thread-local-storage, which the shim needs.
# https://github.com/shadow/shadow/issues/1559
# add_shadow_tests(BASENAME clone)

# The clone test exercises some corner cases in memory management, particularly
# when the thread leader exits before all the threads. Useful to test it without
# the memory manager (really the MemoryMapper) enabled.
#
# FIXME: This test doesn't implement native thread-local-storage, which the shim needs.
# https://github.com/shadow/shadow/issues/1559
# add_shadow_tests(BASENAME clone-nomm SKIP_METHODS preload ARGS --use-memory-manager=false)