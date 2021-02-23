add_linux_tests(BASENAME getsockname COMMAND sh -c "../../target/debug/test_getsockname --libc-passing")
add_shadow_tests(BASENAME getsockname METHODS ptrace-only ptrace preload)