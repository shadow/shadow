add_linux_tests(
    BASENAME nanosleep
    COMMAND sh -c "../../../target/debug/test_nanosleep --libc-passing"
)
add_shadow_tests(BASENAME nanosleep)
