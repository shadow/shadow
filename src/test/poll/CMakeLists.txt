## register the tests
add_test(NAME poll COMMAND ../target/debug/test_poll --libc-passing)
list(APPEND TestNames poll)
# FIXME add_test(
# FIXME     NAME poll-shadow-ptrace
# FIXME     COMMAND sh -c "\
# FIXME     ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
# FIXME     ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d poll.shadow-ptrace.data - \
# FIXME     "
# FIXME )
# FIXME add_test(
# FIXME     NAME poll-shadow-preload
# FIXME     COMMAND sh -c "\
# FIXME     ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
# FIXME     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d poll.shadow-preload.data - \
# FIXME     "
# FIXME )

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
