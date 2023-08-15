add_linux_tests(BASENAME socket COMMAND sh -c "../../../target/debug/test_socket --libc-passing")

add_shadow_tests(BASENAME socket)
add_shadow_tests(
    BASENAME socket-new-tcp
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/socket.yaml"
    ARGS --use-new-tcp true)
