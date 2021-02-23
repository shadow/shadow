add_linux_tests(BASENAME eventfd COMMAND sh -c "../target/debug/test_eventfd --libc-passing")
add_shadow_tests(BASENAME eventfd METHODS ptrace-only ptrace preload)