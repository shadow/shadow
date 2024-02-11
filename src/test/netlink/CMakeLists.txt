add_linux_tests(
    BASENAME netlink-ip-addr
    COMMAND ip addr
    CONFIGURATIONS extra)
add_shadow_tests(
    BASENAME netlink-ip-addr
    POST_CMD "diff hosts/node/*.stdout ${CMAKE_CURRENT_SOURCE_DIR}/ip-addr.stdout"
    CONFIGURATIONS extra)

add_linux_tests(
    BASENAME netlink-send-limit
    COMMAND ../../target/debug/test_netlink_send_limit --libc-passing)
add_shadow_tests(BASENAME netlink-send-limit)
