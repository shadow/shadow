add_shadow_tests(BASENAME signal_resched)
add_shadow_tests(BASENAME exit_after_signal_sched)
add_shadow_tests(BASENAME small_stop_time CHECK_RETVAL FALSE)

add_executable(test_flush_after_exit test_flush_after_exit.c)
add_linux_tests(BASENAME flush_after_exit COMMAND bash -c "test `./test_flush_after_exit` == 'Hello'")
add_shadow_tests(BASENAME flush_after_exit POST_CMD "test `cat hosts/*/*.stdout` = 'Hello'")
add_linux_tests(BASENAME busy_wait COMMAND ../target/debug/test_busy_wait)
add_shadow_tests(
    BASENAME busy_wait
    PROPERTIES
      # This test should be very fast when it succeeds, but will generally take
      # the full timeout to fail otherwise.
      TIMEOUT 5
    )