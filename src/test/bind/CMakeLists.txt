## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-bind shd-test-bind.c)

## create and install an executable that can run outside of shadow
add_executable(test-bind shd-test-bind.c)

## register the tests
add_test(NAME test-bind COMMAND test-bind)
add_test(NAME test-bind-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/bind.test.shadow.config.xml)
