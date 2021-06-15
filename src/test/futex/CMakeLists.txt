include_directories(${GLIB_INCLUDES})
add_executable(test-futex test_futex.c ../test_common.c)
target_link_libraries(test-futex ${CMAKE_THREAD_LIBS_INIT} ${GLIB_LIBRARIES} logger)
add_linux_tests(BASENAME futex COMMAND test-futex)
add_shadow_tests(BASENAME futex METHODS hybrid ptrace)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
# FIXME: The interposed libc does not handle this either.
#add_shadow_tests(BASENAME futex METHODS preload CONFIGURATIONS ilibc)