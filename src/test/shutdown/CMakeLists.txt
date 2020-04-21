## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-shutdown test_shutdown.c ../test_common.c)

## create and install an executable that can run outside of shadow
add_executable(test-shutdown test_shutdown.c ../test_common.c)

## register the tests
add_test(NAME shutdown COMMAND test-shutdown)
add_test(NAME shutdown-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d shutdown.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/shutdown.test.shadow.config.xml)
