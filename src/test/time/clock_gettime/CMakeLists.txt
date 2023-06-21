add_linux_tests(
    BASENAME clock_gettime
    COMMAND sh -c "../../../target/debug/test_clock_gettime --libc-passing"
)
add_shadow_tests(BASENAME clock_gettime)
