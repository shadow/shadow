## build the test as a dynamic executable
add_executable(test-nothing test_nothing.c)

## register the tests
add_test(NAME multihost-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d multihost.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/multihost.test.shadow.config.xml)

add_test(NAME multihost-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d multihost.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/multihost.test.shadow.config.xml)
