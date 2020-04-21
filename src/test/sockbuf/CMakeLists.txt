## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-sockbuf test_sockbuf.c ../test_common.c)

## create and install an executable that can run outside of shadow
add_executable(test-sockbuf test_sockbuf.c ../test_common.c)

## register the tests
add_test(NAME sockbuf COMMAND test-sockbuf)
add_test(NAME sockbuf-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d sockbuf.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/sockbuf.test.shadow.config.xml)
