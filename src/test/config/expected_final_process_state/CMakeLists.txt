# We can test many successful expectations in one go
add_shadow_tests(BASENAME expected_final_process_state_success)

# Failed "expect signal"
add_shadow_tests(
    BASENAME expected_final_process_state_expect_signal_get_wrong_signal
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")
add_shadow_tests(
    BASENAME expected_final_process_state_expect_signal_get_exit
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")
add_shadow_tests(
    BASENAME expected_final_process_state_expect_signal_get_running
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")

# Failed "expect exit"
add_shadow_tests(
    BASENAME expected_final_process_state_expect_exit_get_wrong_exit
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")
add_shadow_tests(
    BASENAME expected_final_process_state_expect_exit_get_signal
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")
add_shadow_tests(
    BASENAME expected_final_process_state_expect_exit_get_running
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")

# Failed "expect running"
add_shadow_tests(
    BASENAME expected_final_process_state_expect_running_get_exit
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")
add_shadow_tests(
    BASENAME expected_final_process_state_expect_running_get_signal
    PROPERTIES PASS_REGULAR_EXPRESSION "1 managed processes in unexpected final state")