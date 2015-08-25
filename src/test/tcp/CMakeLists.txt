## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-tcp shd-test-tcp.c)

## register the tests

## tcp blocking, lossless and lossy
add_test(
    NAME test-tcp-blocking-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-blocking-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossy.test.shadow.config.xml
)

## tcp nonblocking poll, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-poll-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-poll-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossy.test.shadow.config.xml
)

## tcp nonblocking epoll, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-epoll-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-epoll-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossy.test.shadow.config.xml
)

## tcp nonblocking select, lossless and lossy
add_test(
    NAME test-tcp-nonblocking-select-lossless-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossless.test.shadow.config.xml
)
add_test(
    NAME test-tcp-nonblocking-select-lossy-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossy.test.shadow.config.xml
)

## tcp loopback
#add_test(NAME test-tcploop-lossless-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcploop-lossless.test.shadow.config.xml)
#add_test(NAME test-tcploop-lossy-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcploop-lossy.test.shadow.config.xml)
