include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(test-unistd test_unistd.c)

# Use `uname` to get the system's node-name; used to test `gethostname`
execute_process(
    COMMAND uname "-n"
    OUTPUT_VARIABLE SYSTEM_NODE_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

## register the tests
add_test(NAME unistd COMMAND test-unistd "${SYSTEM_NODE_NAME}")
add_test(NAME unistd-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d unistd.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/unistd.test.shadow.config.xml)
