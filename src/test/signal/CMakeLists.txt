include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})

add_executable(test-signal test_signal.c)
target_link_libraries(test-signal ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## register the tests
## here we are testing 3 nodes to make sure they don't share signal handlers (in Shadow)
add_test(NAME signal COMMAND shadow-test-launcher test-signal : test-signal : test-signal)
add_test(NAME signal-shadow-ptrace COMMAND shadow-test-launcher-fail ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d signal.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/signal.test.shadow.config.xml)
# FIXME add_test(NAME signal-shadow-preload COMMAND shadow-test-launcher-fail ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d signal.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/signal.test.shadow.config.xml)
