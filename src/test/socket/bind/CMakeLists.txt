# runs the shadow tests with only the passing tests, not all tests
add_test(NAME bind COMMAND ../../target/debug/test_bind)
add_test(NAME bind-shadow-ptrace COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_bind' -D xARGS='--shadow-passing' ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d bind.shadow-ptrace.data /dev/stdin")
add_test(NAME bind-shadow-preload COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_bind' -D xARGS='--shadow-passing' ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d bind.shadow-preload.data /dev/stdin")
set_property(TEST bind bind-shadow-ptrace bind-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
