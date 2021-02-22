add_linux_tests(BASENAME sleep COMMAND sh -c "../target/debug/test_sleep")
add_shadow_tests(BASENAME sleep METHODS ptrace-only ptrace preload)