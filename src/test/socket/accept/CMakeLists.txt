add_linux_tests(BASENAME accept COMMAND sh -c "../../../target/debug/test_accept --libc-passing")

add_shadow_tests(BASENAME accept LOGLEVEL debug)
add_shadow_tests(
    BASENAME accept-new-tcp
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/accept.yaml"
    LOGLEVEL debug
    ARGS --use-new-tcp true)
