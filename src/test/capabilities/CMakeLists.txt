add_linux_tests(
    BASENAME capget
    COMMAND ../../target/debug/test_capget --libc-passing)
add_shadow_tests(BASENAME capget)

add_linux_tests(
    BASENAME capset
    COMMAND ../../target/debug/test_capset --libc-passing)
add_shadow_tests(BASENAME capset)
