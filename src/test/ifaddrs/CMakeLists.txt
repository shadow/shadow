add_linux_tests(BASENAME ifaddrs COMMAND sh -c "../target/debug/test_ifaddrs 127.0.0.1")
add_shadow_tests(BASENAME ifaddrs METHODS ptrace-only ptrace preload)