include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable
add_executable(test-mmap test_mmap.c)

## register the tests
add_test(NAME mmap COMMAND test-mmap)
add_test(
    NAME mmap-shadow-ptrace
    COMMAND sh -c "\
    ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/mmap.test.shadow.config.yaml --output - \
    | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d mmap.shadow-ptrace.data - \
    "
)
## FIXME: add_test(NAME mmap-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d mmap.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/mmap.test.shadow.config.xml)
