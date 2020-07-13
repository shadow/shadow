# add the shadow sleep tests
add_test(NAME sleep COMMAND ../target/debug/test_sleep)
add_test(NAME sleep-shadow-ptrace COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_sleep' -D xTIMEOUT=10 ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d sleep.shadow-ptrace.data /dev/stdin")
add_test(NAME sleep-shadow-preload COMMAND bash -c
    "m4 -D xBIN='${CMAKE_BINARY_DIR}/src/test/target/debug/test_sleep' -D xTIMEOUT=10 ${CMAKE_SOURCE_DIR}/src/test/shadow_config_template.m4 \
     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d sleep.shadow-preload.data /dev/stdin")
set_property(TEST sleep sleep-shadow-ptrace sleep-shadow-preload PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
