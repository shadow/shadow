add_linux_tests(BASENAME getsockname COMMAND sh -c "../../../target/debug/test_getsockname --libc-passing")

add_shadow_tests(BASENAME getsockname)
add_shadow_tests(
    BASENAME getsockname-new-tcp
    SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/getsockname.yaml"
    ARGS --use-new-tcp true
    )
