find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-phold shd-test-phold.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-phold ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-phold-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
