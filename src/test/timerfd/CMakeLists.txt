## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-timerfd test_timerfd.c)

## create and install an executable that can run outside of shadow
add_executable(test-timerfd test_timerfd.c)

## register the tests
add_test(NAME timerfd COMMAND test-timerfd)
add_test(NAME timerfd-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d timerfd.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/timerfd.test.shadow.config.xml)
