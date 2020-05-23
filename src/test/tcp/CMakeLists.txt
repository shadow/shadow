include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-tcp test_tcp.c)

## register the tests

## tcp blocking - loopback, lossless and lossy
## these also test localhost instead of 127.0.0.1 in the loopback tests
add_test(
    NAME tcp-blocking-loopback
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ../shadow-test-launcher test-tcp blocking server : test-tcp blocking client localhost
)

add_test(
    NAME tcp-blocking-loopback-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d blocking-loopback.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-loopback.test.shadow.config.xml
)
# FIXME add_test(
# FIXME     NAME tcp-blocking-loopback-shadow-preload
# FIXME     COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d blocking-loopback.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-loopback.test.shadow.config.xml
# FIXME )

add_test(
    NAME tcp-blocking-lossless-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d blocking-lossless.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossless.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-blocking-lossy-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d blocking-lossy.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossy.test.shadow.config.xml
)
# FIXME add preload test

## tcp nonblocking poll - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-poll-loopback
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ../shadow-test-launcher test-tcp nonblocking-poll server : test-tcp nonblocking-poll client 127.0.0.1
)
add_test(
    NAME tcp-nonblocking-poll-loopback-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-poll-loopback.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-loopback.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-poll-lossless-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-poll-lossless.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossless.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-poll-lossy-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-poll-lossy.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossy.test.shadow.config.xml
)
# FIXME add preload test

## tcp nonblocking epoll - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-epoll-loopback
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ../shadow-test-launcher test-tcp nonblocking-epoll server : test-tcp nonblocking-epoll client 127.0.0.1
)
add_test(
    NAME tcp-nonblocking-epoll-loopback-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-epoll-loopback.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-loopback.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-epoll-lossless-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-epoll-lossless.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossless.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-epoll-lossy-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-epoll-lossy.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossy.test.shadow.config.xml
)
# FIXME add preload test

## tcp nonblocking select - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-select-loopback
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ../shadow-test-launcher test-tcp nonblocking-select server : test-tcp nonblocking-select client 127.0.0.1
)
add_test(
    NAME tcp-nonblocking-select-loopback-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-select-loopback.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-loopback.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-select-lossless-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-select-lossless.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossless.test.shadow.config.xml
)
# FIXME add preload test
add_test(
    NAME tcp-nonblocking-select-lossy-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d nonblocking-select-lossy.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossy.test.shadow.config.xml
)
# FIXME add preload test

add_test(
    NAME tcp-iov
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ../shadow-test-launcher test-tcp iov server : test-tcp iov client 127.0.0.1
)
add_test(
    NAME tcp-iov-shadow-ptrace
    COMMAND ${CMAKE_SOURCE_DIR}/src/test/tcp/with_q.sh ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d iov.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-iov.test.shadow.config.xml
)
# FIXME add preload test

set_tests_properties(
  tcp-blocking-loopback tcp-nonblocking-poll-loopback tcp-nonblocking-epoll-loopback tcp-nonblocking-select-loopback tcp-iov
  PROPERTIES RUN_SERIAL true
)
