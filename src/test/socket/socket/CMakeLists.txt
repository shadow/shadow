add_linux_tests(BASENAME socket COMMAND sh -c "../../target/debug/test_socket --libc-passing")
add_shadow_tests(BASENAME socket METHODS ptrace-only ptrace preload)