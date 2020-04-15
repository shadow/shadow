find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(test-udp shd-test-udp.c)

## register the tests
add_test(NAME udp COMMAND /bin/bash -c "rm -f udp-fifo && mkfifo udp-fifo && ../shadow-test-launcher test-udp client 0 udp-fifo : test-udp server 0 udp-fifo")
add_test(NAME udp-shadow COMMAND /bin/bash -c "rm -f udp-shadow-fifo && mkfifo udp-shadow-fifo && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d udp.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml")
