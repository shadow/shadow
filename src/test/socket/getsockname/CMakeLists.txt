# runs the shadow tests with only the passing tests, not all tests
add_test(NAME getsockname COMMAND ../../target/debug/test_getsockname)
add_test(NAME getsockname-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d getsockname.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/getsockname.shadow.config.xml)
add_test(NAME getsockname-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d getsockname.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/getsockname.shadow.config.xml)
set_property(TEST getsockname getsockname-shadow-ptrace getsockname-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
