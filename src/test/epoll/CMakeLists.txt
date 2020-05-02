## build the test as a dynamic executable that plugs into shadow
add_library(shadow-plugin-test-epoll SHARED test_epoll.c)
add_library(shadow-plugin-test-epoll-writeable SHARED test_epoll_writeable.c)

## create and install an executable that can run outside of shadow
add_executable(test-epoll test_epoll.c)

## register the tests
add_test(NAME epoll COMMAND test-epoll)
add_test(NAME epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d epoll.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)
add_test(NAME epoll-writeable-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d epoll-writeable.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll-writeable.test.shadow.config.xml)
