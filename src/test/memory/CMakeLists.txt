## register the tests
add_test(NAME mmap COMMAND ../target/debug/test_mmap --libc-passing)
list(APPEND TestNames mmap)
add_test(
    NAME mmap-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/mmap.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d mmap.shadow-ptrace.data - \
    "
)
## FIXME: add_test(NAME mmap-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d mmap.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/mmap.test.shadow.config.xml)

list(APPEND TestNames mmap-shadow-ptrace)
## FIXME: list(APPEND TestNames mmap-shadow-preload)

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902) 
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
