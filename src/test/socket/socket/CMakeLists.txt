# runs the shadow tests with only the passing tests, not all tests
add_test(NAME socket COMMAND ../../target/debug/test_socket)
add_test(NAME socket-shadow-ptrace COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_socket' -D xARGS='--shadow-passing' ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d socket.shadow-ptrace.data /dev/stdin")
add_test(NAME socket-shadow-preload COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_socket' -D xARGS='--shadow-passing' ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d socket.shadow-preload.data /dev/stdin")
set_property(TEST socket socket-shadow-ptrace socket-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
