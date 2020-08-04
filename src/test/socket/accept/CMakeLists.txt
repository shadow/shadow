# runs the shadow tests with only the passing tests, not all tests
add_test(NAME accept COMMAND ../../target/debug/test_accept)
add_test(NAME accept-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d accept.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/accept.shadow.config.xml)
add_test(NAME accept-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d accept.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/accept.shadow.config.xml)
set_property(TEST accept accept-shadow-ptrace accept-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
