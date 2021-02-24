add_linux_tests(BASENAME socketpair COMMAND sh -c "../../target/debug/test_socketpair --libc-passing")
add_shadow_tests(BASENAME socketpair METHODS hybrid ptrace preload)