include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(test-udp test_udp.c)
add_shadow_exe(test-udp-uniprocess test_udp_uniprocess.c)

## register the tests
add_test(NAME udp-uniprocess COMMAND test-udp-uniprocess)
add_test(NAME udp-uniprocess-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d udp-uniprocess.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp-uniprocess.test.shadow.config.xml)

add_test(NAME udp COMMAND /bin/bash -c "rm -f udp-fifo && mkfifo udp-fifo && ../shadow-test-launcher test-udp client 0 udp-fifo : test-udp server 0 udp-fifo")
add_test(NAME udp-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d udp.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml)
