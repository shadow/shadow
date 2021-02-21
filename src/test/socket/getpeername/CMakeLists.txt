add_linux_tests(BASENAME getpeername COMMAND sh -c "../../target/debug/test_getpeername --libc-passing")
add_shadow_tests(BASENAME getpeername METHODS hybrid ptrace preload)