add_linux_tests(BASENAME send-recv COMMAND sh -c "../../../target/debug/test_send_recv --libc-passing")
add_shadow_tests(BASENAME send-recv LOGLEVEL debug)
