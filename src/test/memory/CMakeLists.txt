add_linux_tests(BASENAME mmap COMMAND sh -c "../target/debug/test_mmap --libc-passing")

# run using different rng seeds since we use mkstemp()
add_shadow_tests(BASENAME mmap METHODS ptrace ARGS "--seed 1")
add_shadow_tests(BASENAME mmap METHODS preload ARGS "--seed 2")

add_linux_tests(BASENAME unaligned COMMAND sh -c "../target/debug/test_unaligned --libc-passing")
add_shadow_tests(BASENAME unaligned)
