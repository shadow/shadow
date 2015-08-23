## various socket tests
find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-socket echo-main.c echo-tcp.c echo-udp.c echo-pipe.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-socket ${GLIB_LIBRARIES})

## register the tests
#add_test(NAME test-sleep COMMAND shadow-plugin-test-sleep)
#add_test(NAME test-sleep-shadow COMMAND shadow ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
add_test(NAME test-socket-all-shadow COMMAND shadow ${CMAKE_CURRENT_SOURCE_DIR}/example.xml)
