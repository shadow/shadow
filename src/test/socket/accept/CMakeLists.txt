add_test(NAME accept COMMAND ../../target/debug/test_accept --libc-passing)
list(APPEND TestNames accept)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(NAME accept-shadow-${Method} COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method}
        -l debug -d accept.shadow-${Method}.data ${CMAKE_CURRENT_SOURCE_DIR}/accept.shadow.config.xml)
    list(APPEND TestNames accept-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
