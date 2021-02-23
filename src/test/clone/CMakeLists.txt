include_directories(${GLIB_INCLUDES})
add_executable(test_clone test_clone.c)
target_compile_options(test_clone PUBLIC "-pthread")
target_link_libraries(test_clone ${GLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
add_linux_tests(BASENAME clone COMMAND test_clone)
add_shadow_tests(BASENAME clone METHODS ptrace-only ptrace)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
# FIXME: The interposed libc does not handle this either.
#add_shadow_tests(BASENAME clone METHODS preload CONFIGURATIONS ilibc)