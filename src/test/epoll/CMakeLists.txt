## dependency on glib
include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## create and install an executable that can run outside of shadow
add_executable(test-epoll test_epoll.c)
add_executable(test-epoll-writeable test_epoll_writeable.c)

## register the tests
add_test(NAME epoll COMMAND test-epoll)


add_test(
    NAME epoll-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d epoll-ptrace.shadow.data - \
    "
)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(
    NAME epoll-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/epoll.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d epoll-preload.shadow.data - \
    "
    CONFIGURATIONS ilibc
)


add_test(
    NAME epoll-writeable-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/epoll-writeable.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d epoll-writeable-ptrace.shadow.data - \
    "
)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(
    NAME epoll-writeable-shadow-preload
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/epoll-writeable.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d epoll-writeable-preload.shadow.data - \
    "
    CONFIGURATIONS ilibc
)

