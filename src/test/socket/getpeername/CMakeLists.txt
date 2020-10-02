add_test(NAME getpeername COMMAND ../../target/debug/test_getpeername --libc-passing)
list(APPEND TestNames getpeername)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(
        NAME getpeername-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/getpeername.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d getpeername.shadow-${Method}.data - \
        "
   )
    list(APPEND TestNames getpeername-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
