## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-epoll shd-test-epoll.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-epoll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-epoll COMMAND shadow-plugin-test-epoll)
add_test(NAME test-epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.xml)
