add_linux_tests(
    BASENAME clock_getres
    COMMAND sh -c "../../../target/debug/test_clock_getres --libc-passing"
)
add_shadow_tests(BASENAME clock_getres)
