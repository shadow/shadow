add_linux_tests(
    BASENAME time
    COMMAND sh -c "../../../target/debug/test_time --libc-passing"
)
add_shadow_tests(BASENAME time)
