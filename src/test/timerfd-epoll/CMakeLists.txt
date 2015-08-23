## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-timerfd-epoll shd-test-timerfd-epoll.c)

## register the tests
add_test(NAME test-timerfd-epoll COMMAND shadow-plugin-test-timerfd-epoll)
add_test(NAME test-timerfd-epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/timerfd-epoll.test.shadow.config.xml)
