add_linux_tests(BASENAME bind COMMAND sh -c "../../target/debug/test_bind --libc-passing")
add_shadow_tests(BASENAME bind METHODS ptrace-only ptrace preload)