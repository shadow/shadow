add_executable(test-futex test_futex.c)
target_link_libraries(test-futex ${CMAKE_THREAD_LIBS_INIT})

## register the tests
add_test(NAME futex COMMAND test-futex)
add_test(
    NAME futex-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/futex.test.shadow.config.yaml --output -\
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d futex.shadow-ptrace.data - \
    "
)
# add_test(NAME futex-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d futex.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/futex.test.shadow.config.xml)
