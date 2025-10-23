add_linux_tests(BASENAME resource COMMAND sh -c "../../target/debug/test_resource")
add_shadow_tests(
    BASENAME resource
    # test_set_fsize_zero depends on strace-logging mode being enabled
    # to correctly regression-test for
    # <https://github.com/shadow/shadow/issues/3681>
    ARGS --strace-logging-mode standard
    )
