add_linux_tests(BASENAME stat COMMAND sh -c "../../target/debug/test_stat --libc-passing")
add_shadow_tests(BASENAME stat)
