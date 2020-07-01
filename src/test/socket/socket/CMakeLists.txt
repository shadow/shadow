# runs the shadow tests with only the passing tests, not all tests
add_test(NAME socket COMMAND ../../target/debug/test_socket)
add_test(NAME socket-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d socket.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/socket.shadow.config.xml)
add_test(NAME socket-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d socket.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/socket.shadow.config.xml)
set_property(TEST socket socket-shadow-ptrace socket-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
