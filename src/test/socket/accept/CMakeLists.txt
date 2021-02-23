add_linux_tests(BASENAME accept COMMAND sh -c "../../target/debug/test_accept --libc-passing")
add_shadow_tests(BASENAME accept METHODS ptrace-only ptrace preload)