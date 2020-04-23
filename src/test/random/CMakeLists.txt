## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-random test_random.c)

## register the tests
add_test(NAME random COMMAND shadow-plugin-test-random)
add_test(NAME random-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d random.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.xml)
