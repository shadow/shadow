include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-unistd test_unistd.c)

## register the tests
add_test(NAME unistd COMMAND bash -c "./test-unistd \"$(uname -s)\" \"$(uname -n)\" \"$(uname -r)\" \"$(uname -v)\" \"$(uname -m)\"")
add_test(NAME unistd-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d unistd.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/unistd.test.shadow.config.xml)
add_test(NAME unistd-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d unistd.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/unistd.test.shadow.config.xml)
