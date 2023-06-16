add_linux_tests(
    BASENAME clock_nanosleep
    COMMAND sh -c "../../../target/debug/test_clock_nanosleep --libc-passing"
)
add_shadow_tests(BASENAME clock_nanosleep)
