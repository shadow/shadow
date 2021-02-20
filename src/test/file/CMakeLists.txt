include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})
add_executable(test-file test_file.c)
add_linux_tests(BASENAME file COMMAND test-file)
add_shadow_tests(BASENAME file METHODS hybrid ptrace)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_shadow_tests(BASENAME file METHODS preload CONFIGURATIONS ilibc)