## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-file shd-test-file.c)

## register the tests
add_test(NAME test-file COMMAND shadow-plugin-test-file)
add_test(NAME test-file-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow ${CMAKE_CURRENT_SOURCE_DIR}/file.test.shadow.config.xml)
