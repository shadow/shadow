include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-udp test_udp.c)
add_executable(test-udp-uniprocess test_udp_uniprocess.c)

## register the tests
add_test(NAME udp-uniprocess COMMAND test-udp-uniprocess)
add_test(NAME udp-uniprocess-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d udp-uniprocess-ptrace.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp-uniprocess.test.shadow.config.xml)
add_test(NAME udp-uniprocess-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d udp-uniprocess-preload.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp-uniprocess.test.shadow.config.xml)

add_test(NAME udp COMMAND /bin/bash -c "rm -f udp-fifo && mkfifo udp-fifo && ../shadow-test-launcher test-udp client localhost:0 udp-fifo : test-udp server localhost:0 udp-fifo")
add_test(NAME udp-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d udp.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml)
## FIXME: add_test(NAME udp-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d udp.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml)
