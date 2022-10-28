add_linux_tests(BASENAME ioctl COMMAND sh -c "../../../target/debug/test_ioctl --libc-passing")
add_shadow_tests(BASENAME ioctl)
