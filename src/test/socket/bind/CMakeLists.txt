add_linux_tests(BASENAME bind COMMAND sh -c "../../target/debug/test_bind --libc-passing")
add_shadow_tests(BASENAME bind METHODS hybrid ptrace preload)

add_shadow_tests(BASENAME bind_in_new_process METHODS hybrid ptrace preload)
