add_linux_tests(BASENAME close_range COMMAND sh -c "../../target/debug/test_close_range --libc-passing")
add_shadow_tests(BASENAME close_range)
