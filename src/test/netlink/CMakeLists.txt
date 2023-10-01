add_linux_tests(
    BASENAME netlink-ip-addr
    COMMAND ip addr
    CONFIGURATIONS extra)
add_shadow_tests(
    BASENAME netlink-ip-addr
    POST_CMD "diff hosts/node/*.stdout ${CMAKE_CURRENT_SOURCE_DIR}/ip-addr.stdout"
    CONFIGURATIONS extra)
