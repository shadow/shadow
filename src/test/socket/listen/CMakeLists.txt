# runs the shadow tests with only the passing tests, not all tests
add_test(NAME listen COMMAND ../../target/debug/test_listen)
add_test(NAME listen-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d listen.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/listen.shadow.config.xml)
add_test(NAME listen-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d listen.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/listen.shadow.config.xml)
set_property(TEST listen listen-shadow-ptrace listen-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
