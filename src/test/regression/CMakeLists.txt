add_shadow_tests(BASENAME signal_resched)
add_shadow_tests(BASENAME exit_after_signal_sched)
add_shadow_tests(BASENAME small_stop_time CHECK_RETVAL FALSE)

# Regression test for https://github.com/shadow/shadow/issues/2152
add_shadow_tests(
    BASENAME packet_after_simulation_end
    # Processes still running at the end.
    CHECK_RETVAL FALSE
    PROPERTIES
      # Requires curl and python
      CONFIGURATIONS extra
    )

# Regression test for https://github.com/shadow/shadow/issues/2151
add_shadow_tests(
  BASENAME sigkill_self
  # Command should die with signal 9, giving exitcode 128+9=137
  POST_CMD "test `cat hosts/*/*.exitcode` -eq 137"
  # Shadow should return failure
  EXPECT_ERROR TRUE
  CHECK_RETVAL FALSE)

add_executable(test_flush_after_exit test_flush_after_exit.c)
add_linux_tests(BASENAME flush_after_exit COMMAND bash -c "test `./test_flush_after_exit` == 'Hello'")
add_shadow_tests(BASENAME flush_after_exit POST_CMD "test `cat hosts/*/*.stdout` = 'Hello'")
add_linux_tests(BASENAME busy_wait COMMAND ../../target/debug/test_busy_wait)
add_shadow_tests(
    BASENAME busy_wait
    # Avoid expensive trace-level logging in busy loop.
    LOGLEVEL debug
    PROPERTIES
      # This test should be very fast when it succeeds, but will generally take
      # the full timeout to fail otherwise.
      TIMEOUT 5
    )

add_subdirectory(2210)
