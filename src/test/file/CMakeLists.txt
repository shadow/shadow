find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-file test_file.c)

## create and install an executable that can run outside of shadow
add_executable(test-file test_file.c)

## register the tests
add_test(NAME file COMMAND test-file)
add_test(NAME file-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d file.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/file.test.shadow.config.xml)
