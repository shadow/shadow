include_directories(${GLIB_INCLUDE_DIRS})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-epoll test_epoll.c ../test_common.c)
add_linux_tests(BASENAME epoll COMMAND test-epoll)
add_shadow_tests(BASENAME epoll)

add_executable(test-epoll-writeable test_epoll_writeable.c)
add_shadow_tests(BASENAME epoll-writeable LOGLEVEL debug)

add_linux_tests(BASENAME epoll-rs COMMAND ../../target/debug/test_epoll --libc-passing)
add_shadow_tests(BASENAME epoll-rs)

set(CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/epoll-edge-rs.yaml")

add_linux_tests(BASENAME epoll-edge-rs COMMAND ../../target/debug/test_epoll_edge --libc-passing)
add_shadow_tests(BASENAME epoll-edge-rs)
add_shadow_tests(BASENAME epoll-edge-rs-new-tcp SHADOW_CONFIG "${CONFIG}" ARGS --use-new-tcp true)
