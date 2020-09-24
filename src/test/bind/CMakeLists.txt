include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES} logger)

add_executable(test-bind test_bind.c ../test_common.c)

## register the tests
add_test(NAME bind COMMAND test-bind)
add_test(
    NAME bind-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/bind.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d bind.shadow-ptrace.data - \
    "
)
add_test(
    NAME bind-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/bind.test.shadow.config.yaml --output -\
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d bind.shadow-preload.data -\
    "
)
