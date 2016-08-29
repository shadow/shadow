## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-file shd-test-file.c)

## create and install an executable that can run outside of shadow
add_executable(test-file shd-test-file.c)

## register the tests
add_test(NAME test-file COMMAND test-file)
add_test(NAME test-file-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/file.test.shadow.config.xml)
