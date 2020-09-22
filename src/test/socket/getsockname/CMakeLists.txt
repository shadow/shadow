add_test(NAME getsockname COMMAND ../../target/debug/test_getsockname --libc-passing)
list(APPEND TestNames getsockname)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(NAME getsockname-shadow-${Method} COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method}
        -l debug -d getsockname.shadow-${Method}.data ${CMAKE_CURRENT_SOURCE_DIR}/getsockname.shadow.config.xml)
    list(APPEND TestNames getsockname-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
