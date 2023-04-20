add_linux_tests(BASENAME exit COMMAND sh -c "../../target/debug/test_exit")
add_shadow_tests(BASENAME exit)

add_executable(test_exit_sigsegv test_exit_sigsegv.c)
add_shadow_tests(BASENAME exit_sigsegv)

add_executable(test_exit_abort test_exit_abort.c)
add_shadow_tests(BASENAME exit_abort)
