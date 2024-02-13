add_linux_tests(BASENAME netlink-bind COMMAND sh -c "../../../../target/debug/test_netlink_bind --libc-passing")

add_shadow_tests(
    BASENAME netlink-bind
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/bind.yaml"
    LOGLEVEL debug
    ARGS --strace-logging-mode off
    PROPERTIES
      # This test can take a bit longer, especially on debug builds of shadow
      TIMEOUT 30
    )
