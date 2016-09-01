## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-poll shd-test-poll.c)

## create and install an executable that can run outside of shadow
add_executable(test-poll shd-test-poll.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-poll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## if the test needs any libraries, link them here
target_link_libraries(test-poll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-poll COMMAND test-poll)
add_test(NAME test-poll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.xml)
