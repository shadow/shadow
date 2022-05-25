add_linux_tests(BASENAME itimer COMMAND sh -c "../target/debug/test_itimer")
add_shadow_tests(BASENAME itimer SKIP_METHODS ptrace)