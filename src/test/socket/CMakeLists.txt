## various socket tests
find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-socket echo-main.c echo-tcp.c echo-udp.c echo-pipe.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-socket ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-socket-pipe-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/pipe.test.shadow.config.xml)
add_test(NAME test-socket-socketpair-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/socketpair.test.shadow.config.xml)
add_test(NAME test-socket-udp-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml)
add_test(NAME test-socket-udploop-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/udploop.test.shadow.config.xml)
add_test(NAME test-socket-tcp-lossless-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcp-lossless.test.shadow.config.xml)
add_test(NAME test-socket-tcploop-lossless-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcploop-lossless.test.shadow.config.xml)
add_test(NAME test-socket-tcp-lossy-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcp-lossy.test.shadow.config.xml)
add_test(NAME test-socket-tcploop-lossy-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/tcploop-lossy.test.shadow.config.xml)