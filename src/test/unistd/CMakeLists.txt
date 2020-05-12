include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-unistd test_unistd.c)

# Use `uname` command-line program to get expected results for native uname(2).
execute_process(
    COMMAND uname "-s"
    OUTPUT_VARIABLE UNAME_SYSNAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND uname "-n"
    OUTPUT_VARIABLE UNAME_NODENAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND uname "-r"
    OUTPUT_VARIABLE UNAME_RELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND uname "-v"
    OUTPUT_VARIABLE UNAME_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND uname "-m"
    OUTPUT_VARIABLE UNAME_MACHINE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

## register the tests
add_test(NAME unistd COMMAND test-unistd "${UNAME_SYSNAME}" "${UNAME_NODENAME}" "${UNAME_RELEASE}" "${UNAME_VERSION}" "${UNAME_MACHINE}")
add_test(NAME unistd-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d unistd.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/unistd.test.shadow.config.xml)
