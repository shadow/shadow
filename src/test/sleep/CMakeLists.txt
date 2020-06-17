# add the shadow sleep tests
add_test(NAME sleep COMMAND ../target/debug/test_sleep)
add_test(NAME sleep-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d sleep.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
add_test(NAME sleep-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d sleep.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
set_property(TEST sleep sleep-shadow-ptrace sleep-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
