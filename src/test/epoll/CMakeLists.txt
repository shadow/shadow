## dependency on glib
include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## create and install an executable that can run outside of shadow
add_executable(test-epoll test_epoll.c)
add_executable(test-epoll-writeable test_epoll_writeable.c)

## register the tests
add_test(NAME epoll COMMAND test-epoll)
add_test(NAME epoll-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d epoll-ptrace.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)
#add_test(NAME epoll-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d epoll-preload.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)

# TODO: re-enable this test once the corresponding socket functions are supported
# TODO: add_test(NAME epoll-writeable-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d epoll-writeable-ptrace.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll-writeable.test.shadow.config.xml)
#add_test(NAME epoll-writeable-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d epoll-writeable-preload.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/epoll-writeable.test.shadow.config.xml)
