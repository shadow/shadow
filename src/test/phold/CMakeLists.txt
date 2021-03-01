include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-phold test_phold.c)

## if the test needs any libraries, link them here
target_link_libraries(test-phold ${M_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

## register the tests
## dont run with debug logging because it causes the test case to take too long
add_test(
    NAME phold-shadow-hybrid
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=hybrid --pin-cpus -d phold.shadow-hybrid.data - \
    "
)

# Run in ptrace mode *without* shim IPC. This is primarily for measurement purposes.
add_test(
    NAME phold-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace --pin-cpus -d phold.shadow-ptrace.data - \
    "
)

# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(
    NAME phold-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload --pin-cpus -d phold.shadow-preload.data -
    "
    CONFIGURATIONS ilibc
)

add_test(
    NAME phold-threaded-shadow-hybrid
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=hybrid --pin-cpus -d phold-threaded.shadow-hybrid.data -w 2 - \
    "
    PROPERTIES RUN_SERIAL true
)

add_test(
    NAME phold-threaded-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace --pin-cpus -d phold-threaded.shadow-ptrace.data -w 2 - \
    "
)

# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(
    NAME phold-threaded-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload --pin-cpus -d phold-threaded.shadow-preload.data -w 2 - \
    "
    CONFIGURATIONS ilibc
)

set_tests_properties(
    phold-shadow-hybrid
    phold-shadow-ptrace
    phold-shadow-preload
    phold-threaded-shadow-hybrid
    phold-threaded-shadow-ptrace
    phold-threaded-shadow-preload
    PROPERTIES
    # These tests use cpu-pinning, which means they'll all try to use the same
    # set of CPUs. i.e. they'll effectively be serialized anyway, but if we
    # allow cmake to *start* them in parallel, some are likely to time out.
    RUN_SERIAL TRUE
    # Even run serially, these tests still need some extra time in the GitHub
    # CI, especially in coverage builds.
    TIMEOUT 60
    )
