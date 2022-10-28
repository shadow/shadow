add_linux_tests(BASENAME mmap COMMAND sh -c "../../target/debug/test_mmap --libc-passing")
add_shadow_tests(BASENAME mmap)

add_linux_tests(BASENAME unaligned COMMAND sh -c "../../target/debug/test_unaligned --libc-passing")
add_shadow_tests(BASENAME unaligned)
