add_test(NAME unistd COMMAND ../target/debug/test_unistd --libc-passing)
list(APPEND TestNames unistd)

foreach(Method ptrace preload)
    add_test(
        NAME unistd-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/unistd.test.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d unistd.shadow-${Method}.data - \
        "
    )
    list(APPEND TestNames unistd-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
