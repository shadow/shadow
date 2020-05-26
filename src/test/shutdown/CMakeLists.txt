add_executable(test-shutdown test_shutdown.c ../test_common.c)

## register the tests
add_test(NAME shutdown COMMAND test-shutdown)
add_test(NAME shutdown-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d shutdown.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/shutdown.test.shadow.config.xml)
# FIXME add_test(NAME shutdown-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d shutdown.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/shutdown.test.shadow.config.xml)
