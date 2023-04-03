include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})
add_executable(test-signal test_signal.c)
target_link_libraries(test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## here we are testing 3 nodes to make sure they don't share signal handlers (in Shadow)
add_linux_tests(BASENAME signal COMMAND shadow-test-launcher test-signal : test-signal : test-signal)
add_shadow_tests(BASENAME signal)

## More complete signal functionality.
add_linux_tests(BASENAME signals-extra COMMAND sh -c "../../target/debug/test_signals --libc-passing")
add_shadow_tests(BASENAME signals-extra)

## Basic cross-process signal tests.
add_shadow_tests(
    BASENAME signals-multiprocess
    # `sleep` should die with signal SIGINT (2), giving exitcode 128+2=130
    POST_CMD "test `cat hosts/*/sleep.*.exitcode` -eq 130"
    # Shadow should return failure
    EXPECT_ERROR TRUE
    CHECK_RETVAL FALSE)
