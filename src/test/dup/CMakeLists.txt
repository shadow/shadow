add_linux_tests(BASENAME dup COMMAND sh -c "../../target/debug/test_dup --libc-passing")
add_shadow_tests(BASENAME dup)
