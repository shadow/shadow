## register the tests
add_test(NAME pthreads COMMAND ../target/debug/test_pthreads --libc-passing)
list(APPEND TestNames pthreads)

add_test(
    NAME pthreads-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/pthreads.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d pthreads.shadow-ptrace.data - \
    "
)

## FIXME: Thread-related syscalls are not currently emulated in Shadow's preload mode.
##        `clone()` is currently unimplemented, which means the clone runs natively, bypassing Shadow
## add_test(
##    NAME pthreads-shadow-preload
##    COMMAND sh -c "\
##     ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/pthreads.test.shadow.config.yaml --output - \
##     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug \ -d pthreads.shadow-preload.data - \
##    "
## )

list(APPEND TestNames pthreads-shadow-ptrace)
## list(APPEND TestNames pthreads-shadow-preload)

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
