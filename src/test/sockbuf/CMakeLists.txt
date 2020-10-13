add_executable(test-sockbuf test_sockbuf.c ../test_common.c)

## register the tests
add_test(NAME sockbuf COMMAND test-sockbuf)
add_test(
    NAME sockbuf-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/sockbuf.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d sockbuf.shadow-ptrace.data - \
    "
)
# add_test(NAME sockbuf-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d sockbuf.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/sockbuf.test.shadow.config.xml)
