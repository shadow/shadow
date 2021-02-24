add_linux_tests(BASENAME pipe COMMAND sh -c "../target/debug/test_pipe --libc-passing")
add_shadow_tests(BASENAME pipe METHODS hybrid ptrace preload)