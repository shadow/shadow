add_linux_tests(BASENAME shutdown COMMAND sh -c "../../../target/debug/test_shutdown --libc-passing")

set(CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/shutdown.yaml")

add_shadow_tests(BASENAME shutdown LOGLEVEL debug)
add_shadow_tests(BASENAME shutdown-new-tcp SHADOW_CONFIG "${CONFIG}" LOGLEVEL debug ARGS --use-new-tcp true)
