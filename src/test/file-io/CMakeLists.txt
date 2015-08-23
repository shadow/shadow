## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-file-io shd-test-file-io.c)

## register the tests
add_test(NAME test-file-io COMMAND shadow-plugin-test-file-io)
add_test(NAME test-file-io-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/file-io.test.shadow.config.xml)
