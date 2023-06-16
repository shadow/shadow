add_linux_tests(
    BASENAME itimer
    COMMAND sh -c "../../../target/debug/test_itimer --libc-passing"
)
add_shadow_tests(BASENAME itimer)

add_linux_tests(
    BASENAME itimer_scheduled_after_exit
    COMMAND sh -c "../../../target/debug/test_itimer_scheduled_after_exit"
)
add_shadow_tests(BASENAME itimer_scheduled_after_exit)