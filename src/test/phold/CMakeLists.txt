find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-phold shd-test-phold.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-phold ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
## dont run with debug logging because it causes the test case to take too long
add_test(NAME phold-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -d phold.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
add_test(NAME phold-threaded-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -d phold-threaded.shadow.data -w 2 ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
