include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-udp test_udp.c)

## register the tests
add_test(NAME udp COMMAND /bin/bash -c "rm -f udp-fifo && mkfifo udp-fifo && ../shadow-test-launcher test-udp client 0 udp-fifo : test-udp server 0 udp-fifo")
add_test(NAME udp-shadow-ptrace COMMAND /bin/bash -c "rm -f udp-shadow-fifo && mkfifo udp-shadow-fifo && ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d udp.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml")
# FIXME add_test(NAME udp-shadow-preload COMMAND /bin/bash -c "rm -f udp-shadow-fifo && mkfifo udp-shadow-fifo && ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d udp.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml")
