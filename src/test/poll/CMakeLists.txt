add_linux_tests(BASENAME poll COMMAND sh -c "../target/debug/test_poll --libc-passing")

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME poll METHODS ptrace ARGS "--seed 1")
add_shadow_tests(BASENAME poll METHODS preload ARGS "--seed 2")
