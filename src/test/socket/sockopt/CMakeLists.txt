add_linux_tests(BASENAME sockopt COMMAND sh -c "../../target/debug/test_sockopt --libc-passing")
add_shadow_tests(BASENAME sockopt METHODS hybrid ptrace preload)