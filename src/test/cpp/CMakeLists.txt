set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=gnu++11")

add_executable(test-cpp test_cpp.cpp)

## register the tests
add_test(NAME cpp COMMAND test-cpp)
add_test(
    NAME cpp-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/cpp.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d cpp.shadow-ptrace.data - \
    "
)
add_test(
    NAME cpp-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/cpp.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d cpp.shadow-preload.data  - \
    "
)
