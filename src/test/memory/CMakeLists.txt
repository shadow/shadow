add_linux_tests(BASENAME mmap COMMAND sh -c "../target/debug/test_mmap --libc-passing")
add_shadow_tests(BASENAME mmap METHODS hybrid ptrace)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_shadow_tests(BASENAME mmap METHODS preload CONFIGURATIONS ilibc)