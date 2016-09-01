## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-epoll shd-test-epoll.c)

## create and install an executable that can run outside of shadow
add_executable(test-epoll shd-test-epoll.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-epoll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## if the test needs any libraries, link them here
target_link_libraries(test-epoll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-epoll COMMAND test-epoll)
add_test(NAME test-epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)
