## build the test as a dynamic executable that plugs into shadow
add_library(shadow-plugin-test-epoll SHARED shd-test-epoll.c)

## create and install an executable that can run outside of shadow
add_executable(test-epoll shd-test-epoll.c)

## register the tests
add_test(NAME test-epoll COMMAND test-epoll)
add_test(NAME test-epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d epoll.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)
