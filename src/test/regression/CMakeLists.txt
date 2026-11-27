add_shadow_tests(BASENAME signal_resched)
add_shadow_tests(BASENAME exit_after_signal_sched)

# Regression test for https://github.com/shadow/shadow/issues/1868.
# Shadow should explicitly fail when a process start time is after
# the simulation end time.
add_shadow_tests(BASENAME small_stop_time EXPECT_ERROR TRUE)

# Regression test for https://github.com/shadow/shadow/issues/2152
add_shadow_tests(
    BASENAME packet_after_simulation_end
    PROPERTIES
      # Requires curl and python
      CONFIGURATIONS extra
    )

# Regression test for https://github.com/shadow/shadow/issues/2151
add_shadow_tests(BASENAME sigkill_self)

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

add_linux_tests(BASENAME cpu_busy_wait COMMAND ../../target/debug/test_cpu_busy_wait)
add_shadow_tests(
    BASENAME cpu_busy_wait
    PROPERTIES
      # This test should be very fast when it succeeds, but will generally take
      # the full timeout to fail otherwise.
      TIMEOUT 5
    )

add_shadow_tests(BASENAME no_tty)
    
add_subdirectory(2210)
add_subdirectory(3100)
add_subdirectory(3148)
add_subdirectory(3650)
