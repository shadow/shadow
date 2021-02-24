add_linux_tests(BASENAME random COMMAND sh -c "../target/debug/test_random --libc-passing")
add_shadow_tests(BASENAME random METHODS hybrid ptrace preload)