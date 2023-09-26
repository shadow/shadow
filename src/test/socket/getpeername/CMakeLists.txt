add_linux_tests(BASENAME getpeername COMMAND sh -c "../../../target/debug/test_getpeername --libc-passing")

add_shadow_tests(BASENAME getpeername)
add_shadow_tests(
    BASENAME getpeername-new-tcp
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/getpeername.yaml"
    ARGS --use-new-tcp true
    )
