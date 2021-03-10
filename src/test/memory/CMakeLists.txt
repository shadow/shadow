add_linux_tests(BASENAME mmap COMMAND sh -c "../target/debug/test_mmap --libc-passing")

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME mmap METHODS hybrid ARGS "--seed 0")
add_shadow_tests(BASENAME mmap METHODS ptrace ARGS "--seed 1")
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_shadow_tests(BASENAME mmap METHODS preload CONFIGURATIONS ilibc ARGS "--seed 2")
