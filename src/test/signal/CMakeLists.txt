include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})
add_executable(test-signal test_signal.c)
target_link_libraries(test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## here we are testing 3 nodes to make sure they don't share signal handlers (in Shadow)
add_linux_tests(BASENAME signal COMMAND shadow-test-launcher test-signal : test-signal : test-signal)
# FIXME: Enable for preload. See https://github.com/shadow/shadow/issues/1455
add_shadow_tests(BASENAME signal SKIP_METHODS preload)