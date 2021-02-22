add_linux_tests(BASENAME connect COMMAND sh -c "../../target/debug/test_connect --libc-passing")
add_shadow_tests(BASENAME connect METHODS ptrace-only ptrace preload)
