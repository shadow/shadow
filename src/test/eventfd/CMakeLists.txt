add_test(NAME eventfd COMMAND ../target/debug/test_eventfd --libc-passing)
list(APPEND TestNames eventfd)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(
        NAME eventfd-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/eventfd.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d eventfd.shadow-${Method}.data -\
        "
    )
    list(APPEND TestNames eventfd-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
