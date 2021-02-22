add_linux_tests(BASENAME listen COMMAND sh -c "../../target/debug/test_listen --libc-passing")
add_shadow_tests(BASENAME listen METHODS ptrace-only ptrace preload)