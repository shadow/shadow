add_linux_tests(BASENAME bind COMMAND sh -c "../../../target/debug/test_bind --libc-passing")

add_shadow_tests(
    BASENAME bind
    LOGLEVEL debug
    ARGS --strace-logging-mode off
    PROPERTIES
      # This test can take a bit longer, especially on debug builds of shadow
      TIMEOUT 30
    )
add_shadow_tests(
    BASENAME bind-new-tcp
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/bind.yaml"
    LOGLEVEL debug
    ARGS --strace-logging-mode off --use-new-tcp true
    PROPERTIES
      # This test can take a bit longer, especially on debug builds of shadow
      TIMEOUT 30
    )

add_shadow_tests(BASENAME bind_in_new_process)
