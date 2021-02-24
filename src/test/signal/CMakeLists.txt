include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})
add_executable(test-signal test_signal.c)
target_link_libraries(test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## here we are testing 3 nodes to make sure they don't share signal handlers (in Shadow)
add_linux_tests(BASENAME signal COMMAND shadow-test-launcher test-signal : test-signal : test-signal)
add_shadow_tests(BASENAME signal METHODS hybrid ptrace)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
# FIXME: The interposed libc does not handle this either.
#add_shadow_tests(BASENAME signal METHODS preload CONFIGURATIONS ilibc)