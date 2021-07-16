include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})
add_executable(test-file test_file.c)
add_linux_tests(BASENAME file COMMAND test-file)

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME file METHODS ptrace ARGS "--seed 1")
add_shadow_tests(BASENAME file METHODS preload ARGS "--seed 2")
