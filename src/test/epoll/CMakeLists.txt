include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-epoll test_epoll.c)
add_linux_tests(BASENAME epoll COMMAND test-epoll)
add_shadow_tests(BASENAME epoll METHODS ptrace-only ptrace preload)

add_executable(test-epoll-writeable test_epoll_writeable.c)
add_shadow_tests(BASENAME epoll-writeable METHODS ptrace-only ptrace preload)