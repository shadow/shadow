## register the tests
add_test(NAME poll COMMAND ../target/debug/test_poll --libc-passing)
list(APPEND TestNames poll)

add_test(
    NAME poll-shadow-ptrace-only
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow -l debug --interpose-method=ptrace-only -l debug -d poll.shadow-ptrace-only.data - \
    "
)
add_test(
    NAME poll-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow -l debug --interpose-method=ptrace -l debug -d poll.shadow-ptrace.data - \
    "
)
add_test(
    NAME poll-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow -l debug --interpose-method=preload -l debug -d poll.shadow-preload.data - \
    "
)

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
