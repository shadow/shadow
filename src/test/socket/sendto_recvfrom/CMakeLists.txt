add_linux_tests(BASENAME sendto-recvfrom COMMAND sh -c "../../../target/debug/test_sendto_recvfrom --libc-passing")
add_shadow_tests(BASENAME sendto-recvfrom LOGLEVEL debug)
