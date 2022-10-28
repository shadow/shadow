add_linux_tests(BASENAME select COMMAND sh -c "../../target/debug/test_select --libc-passing")
add_shadow_tests(BASENAME select)
