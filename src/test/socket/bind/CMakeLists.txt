add_test(NAME bind COMMAND ../../target/debug/test_bind --libc-passing)
list(APPEND TestNames bind)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(
        NAME bind-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/bind.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d bind.shadow-${Method}.data - \
        "
    )
    list(APPEND TestNames bind-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
