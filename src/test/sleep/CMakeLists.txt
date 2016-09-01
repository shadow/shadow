## if this test needs any libraries, find and include them here
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)
include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-sleep shd-test-sleep.c)

## create and install an executable that can run outside of shadow
add_executable(test-sleep shd-test-sleep.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-sleep ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
target_link_libraries(test-sleep ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-sleep COMMAND test-sleep)
add_test(NAME test-sleep-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
