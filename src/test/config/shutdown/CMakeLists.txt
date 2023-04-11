add_shadow_tests(
    BASENAME shutdown_signal_sigint
    # `sleep` should die with signal SIGINT (2), giving exitcode 128+2=130
    POST_CMD "test `cat hosts/*/sleep.*.exitcode` -eq 130"
    # Shadow should return failure
    EXPECT_ERROR TRUE
    CHECK_RETVAL FALSE)

add_shadow_tests(
    BASENAME shutdown_signal_sigterm
    # `sleep` should die with signal SIGTERM (15), giving exitcode 128+15=143
    POST_CMD "test `cat hosts/*/sleep.*.exitcode` -eq 143"
    # Shadow should return failure
    EXPECT_ERROR TRUE
    CHECK_RETVAL FALSE)

add_shadow_tests(
    BASENAME shutdown_signal_sigkill
    # `sleep` should die with signal SIGKILL (9), giving exitcode 128+9=137
    POST_CMD "test `cat hosts/*/sleep.*.exitcode` -eq 137"
    # Shadow should return failure
    EXPECT_ERROR TRUE
    CHECK_RETVAL FALSE)