## register the tests
add_test(NAME random COMMAND ../target/debug/test_random --libc-passing)
list(APPEND TestNames random)

add_test(
    NAME random-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d random.shadow-ptrace.data - \
    "
)
add_test(
    NAME random-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d random.shadow-preload.data - \
    "
)

list(APPEND TestNames random-shadow-ptrace)
list(APPEND TestNames random-shadow-preload)

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
