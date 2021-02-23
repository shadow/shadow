add_linux_tests(BASENAME poll COMMAND sh -c "../target/debug/test_poll --libc-passing")
add_shadow_tests(BASENAME poll METHODS ptrace-only ptrace preload)