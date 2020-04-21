## if this test needs any libraries, find and include them here
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-signal test_signal.c)

## create and install an executable that can run outside of shadow
add_executable(test-signal test_signal.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})
target_link_libraries(test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## register the tests
## here we are testing 3 nodes to make sure they don't share signal handlers (in Shadow)
add_test(NAME signal COMMAND shadow-test-launcher test-signal : test-signal : test-signal)
add_test(NAME signal-shadow COMMAND shadow-test-launcher-fail ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d signal.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/signal.test.shadow.config.xml)
