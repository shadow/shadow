add_linux_tests(BASENAME exit COMMAND sh -c "../../target/debug/test_exit")
add_shadow_tests(BASENAME exit)

add_executable(test_exit_sigsegv test_exit_sigsegv.c)
# Expect the managed process to exit with with 139 (128 + SIGSEGV).
add_shadow_tests(BASENAME exit_sigsegv EXPECT_ERROR true CHECK_RETVAL false POST_CMD "test `cat hosts/*/*.exitcode` -eq 139")

add_executable(test_exit_abort test_exit_abort.c)
# Expect the managed process to exit with with 134 (128 + SIGABRT)
add_shadow_tests(BASENAME exit_abort EXPECT_ERROR true CHECK_RETVAL false POST_CMD "test `cat hosts/*/*.exitcode` -eq 134")
