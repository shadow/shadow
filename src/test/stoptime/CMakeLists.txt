# tests to make sure that if we reach the stop time, shadow exits with an error message
add_test(NAME stoptime COMMAND timeout 1 /bin/sleep 2)
add_test(NAME stoptime-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d stoptime.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/stoptime.shadow.config.xml)
add_test(NAME stoptime-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d stoptime.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/stoptime.shadow.config.xml)

# we expect the tests to have a non-zero exit code
set_tests_properties(stoptime stoptime-shadow-ptrace stoptime-shadow-preload PROPERTIES WILL_FAIL true)
