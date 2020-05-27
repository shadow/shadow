include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES} logger)

add_executable(test-bind test_bind.c)

## register the tests
add_test(NAME bind COMMAND test-bind)
add_test(NAME bind-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d bind.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/bind.test.shadow.config.xml)
add_test(NAME bind-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d bind.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/bind.test.shadow.config.xml)
