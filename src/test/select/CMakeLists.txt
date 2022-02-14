add_linux_tests(BASENAME select COMMAND sh -c "../target/debug/test_select --libc-passing")

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME select METHODS ptrace ARGS "--seed 1")
add_shadow_tests(BASENAME select METHODS preload ARGS "--seed 2")
