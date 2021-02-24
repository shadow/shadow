add_linux_tests(BASENAME shutdown COMMAND sh -c "../../target/debug/test_shutdown --libc-passing")
add_shadow_tests(BASENAME shutdown METHODS hybrid ptrace preload)