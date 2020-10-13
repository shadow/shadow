include_directories(${RT_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test-pthreads test_pthreads.c)

## if the test needs any libraries, link them here
target_link_libraries(test-pthreads ${CMAKE_THREAD_LIBS_INIT})

## register the tests
add_test(NAME pthreads COMMAND test-pthreads)
add_test(
    NAME pthreads-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/pthreads.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d pthreads.shadow-ptrace.data - \
    "
)
