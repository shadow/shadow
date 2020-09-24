add_executable(test-timerfd test_timerfd.c)

## register the tests
add_test(NAME timerfd COMMAND test-timerfd)
add_test(
    NAME timerfd-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/timerfd.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d timerfd.shadow-ptrace.data - \
    "
)
# FIXME add_test(NAME timerfd-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d timerfd.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/timerfd.test.shadow.config.xml)
