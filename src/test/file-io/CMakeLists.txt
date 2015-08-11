## if this test needs any libraries, find and include them here
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)
include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-file-io shd-test-file-io.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-file-io ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-file-io COMMAND shadow-plugin-test-file-io)
add_test(NAME test-file-io-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/file-io.test.shadow.config.xml)
