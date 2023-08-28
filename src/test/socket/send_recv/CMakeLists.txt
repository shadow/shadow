add_linux_tests(BASENAME send-recv COMMAND sh -c "../../../target/debug/test_send_recv --libc-passing")

set(CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/send-recv.yaml")

add_shadow_tests(BASENAME send-recv LOGLEVEL debug)
add_shadow_tests(BASENAME send-recv-new-tcp LOGLEVEL debug SHADOW_CONFIG "${CONFIG}" ARGS --use-new-tcp true)
