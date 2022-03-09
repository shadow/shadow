add_linux_tests(BASENAME bind COMMAND sh -c "../../target/debug/test_bind --libc-passing")
add_shadow_tests(BASENAME bind LOGLEVEL debug ARGS --strace-logging-mode off)

add_shadow_tests(BASENAME bind_in_new_process CHECK_RETVAL false)
