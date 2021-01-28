set(BaseName sockopt)

add_test(NAME ${BaseName} COMMAND ../../target/debug/test_${BaseName} --libc-passing)
list(APPEND TestNames ${BaseName})

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(
        NAME ${BaseName}-shadow-${Method}
        COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/${BaseName}.shadow.config.yaml --output - \
        | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method} -l debug -d ${BaseName}.shadow-${Method}.data - \
        "
    )
    list(APPEND TestNames ${BaseName}-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
