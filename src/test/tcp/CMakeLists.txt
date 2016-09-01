## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-tcp shd-test-tcp.c)

## create and install an executable that can run outside of shadow
add_executable(test-tcp shd-test-tcp.c)

## register the tests

## tcp blocking - loopback, lossless and lossy
## these also test "localhost" instead of "127.0.0.1" in the loopback tests
add_test(
    NAME test-tcp-blocking-loopback
    COMMAND shadow-test-launcher test-tcp blocking server : test-tcp blocking client localhost
)
add_test(
    NAME test-tcp-blocking-loopback-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-loopback.test.shadow.config.xml
)
add_test(
    NAME test-tcp-blocking-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-blocking-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossy.test.shadow.config.xml
)

## tcp nonblocking poll - loopback, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-poll-loopback
    COMMAND shadow-test-launcher test-tcp nonblocking-poll server : test-tcp nonblocking-poll client 127.0.0.1
)
add_test(
    NAME test-tcp-nonblocking-poll-loopback-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-loopback.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-poll-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-poll-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossy.test.shadow.config.xml
)

## tcp nonblocking epoll - loopback, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-epoll-loopback
    COMMAND shadow-test-launcher test-tcp nonblocking-epoll server : test-tcp nonblocking-epoll client 127.0.0.1
)
add_test(
    NAME test-tcp-nonblocking-epoll-loopback-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-loopback.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-epoll-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-epoll-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossy.test.shadow.config.xml
)

## tcp nonblocking select - loopback, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-select-loopback
    COMMAND shadow-test-launcher test-tcp nonblocking-select server : test-tcp nonblocking-select client 127.0.0.1
)
add_test(
    NAME test-tcp-nonblocking-select-loopback-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-loopback.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-select-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-select-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossy.test.shadow.config.xml
)

add_test(
    NAME test-tcp-iov
    COMMAND shadow-test-launcher test-tcp iov server : test-tcp iov client 127.0.0.1
)
add_test(
    NAME test-tcp-iov-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-iov.test.shadow.config.xml
)
