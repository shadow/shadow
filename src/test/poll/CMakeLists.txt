add_linux_tests(BASENAME poll COMMAND sh -c "../target/debug/test_poll --libc-passing")

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME poll METHODS hybrid ARGS "--seed 0")
add_shadow_tests(BASENAME poll METHODS ptrace ARGS "--seed 1")
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_shadow_tests(BASENAME poll METHODS preload CONFIGURATIONS ilibc ARGS "--seed 2")
