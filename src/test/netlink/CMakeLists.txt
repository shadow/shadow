include_directories(${GLIB_INCLUDE_DIRS})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-send-limit test_send_limit.c)

add_shadow_tests(BASENAME netlink-send-limit)

add_linux_tests(
    BASENAME netlink-ip-addr
    COMMAND ip addr
    CONFIGURATIONS extra)
add_shadow_tests(
    BASENAME netlink-ip-addr
    POST_CMD "diff hosts/node/*.stdout ${CMAKE_CURRENT_SOURCE_DIR}/ip-addr.stdout"
    CONFIGURATIONS extra)
