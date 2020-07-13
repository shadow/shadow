# runs the shadow tests with only the passing tests, not all tests
add_test(NAME bind COMMAND ../../target/debug/test_bind)
add_test(NAME bind-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d bind.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/bind.shadow.config.xml)
add_test(NAME bind-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d bind.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/bind.shadow.config.xml)
set_property(TEST bind bind-shadow-ptrace bind-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
