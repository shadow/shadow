add_linux_tests(BASENAME prctl COMMAND sh -c "../../target/debug/test_prctl --libc-passing")
add_shadow_tests(BASENAME prctl)
