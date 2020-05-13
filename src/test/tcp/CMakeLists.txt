include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-tcp test_tcp.c)

## create and install an executable that can run outside of shadow
add_executable(test-tcp test_tcp.c)

## register the tests

## tcp blocking - loopback, lossless and lossy
## these also test "localhost" instead of "127.0.0.1" in the loopback tests
add_test(
    NAME tcp-blocking-loopback
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ../shadow-test-launcher test-tcp blocking $QUEUE server : test-tcp blocking $QUEUE client localhost; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-blocking-loopback-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d blocking-loopback.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-loopback.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-blocking-lossless-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d blocking-lossless.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossless.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-blocking-lossy-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d blocking-lossy.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-blocking-lossy.test.shadow.config.xml; ipcrm -q $QUEUE"
)

## tcp nonblocking poll - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-poll-loopback
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ../shadow-test-launcher test-tcp nonblocking-poll $QUEUE server : test-tcp nonblocking-poll $QUEUE client 127.0.0.1; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-poll-loopback-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-poll-loopback.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-loopback.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-poll-lossless-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-poll-lossless.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossless.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-poll-lossy-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-poll-lossy.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-poll-lossy.test.shadow.config.xml; ipcrm -q $QUEUE"
)

## tcp nonblocking epoll - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-epoll-loopback
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ../shadow-test-launcher test-tcp nonblocking-epoll $QUEUE server : test-tcp nonblocking-epoll $QUEUE client 127.0.0.1; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-epoll-loopback-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-epoll-loopback.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-loopback.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-epoll-lossless-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-epoll-lossless.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossless.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-epoll-lossy-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-epoll-lossy.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-epoll-lossy.test.shadow.config.xml; ipcrm -q $QUEUE"
)

## tcp nonblocking select - loopback, lossless and lossy
add_test(
    NAME tcp-nonblocking-select-loopback
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ../shadow-test-launcher test-tcp nonblocking-select $QUEUE server : test-tcp nonblocking-select $QUEUE client 127.0.0.1; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-select-loopback-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-select-loopback.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-loopback.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-select-lossless-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-select-lossless.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossless.test.shadow.config.xml; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-nonblocking-select-lossy-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d nonblocking-select-lossy.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-nonblocking-select-lossy.test.shadow.config.xml; ipcrm -q $QUEUE"
)

add_test(
    NAME tcp-iov
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ../shadow-test-launcher test-tcp iov $QUEUE server : test-tcp iov $QUEUE client 127.0.0.1; ipcrm -q $QUEUE"
)
add_test(
    NAME tcp-iov-shadow
    COMMAND /bin/bash -c "QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'` && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d iov.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/tcp-iov.test.shadow.config.xml; ipcrm -q $QUEUE"
)

set_tests_properties(
  tcp-blocking-loopback tcp-nonblocking-poll-loopback tcp-nonblocking-epoll-loopback tcp-nonblocking-select-loopback tcp-iov
  PROPERTIES RUN_SERIAL true
)
