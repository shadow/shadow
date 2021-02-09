add_test(
    NAME ifaddrs
    COMMAND sh -c "../target/debug/test_ifaddrs 127.0.0.1"
)
list(APPEND TestNames ifaddrs)

foreach(Method ptrace preload)
    add_test(
        NAME ifaddrs-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/ifaddrs.test.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d ifaddrs.shadow-${Method}.data - \
        "
    )
    list(APPEND TestNames ifaddrs-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
