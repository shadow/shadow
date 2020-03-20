include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test-sleep test_sleep.c)

## if the test needs any libraries, link them here
target_link_libraries(test-sleep ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## register the tests
add_test(NAME sleep COMMAND test-sleep)
add_test(NAME sleep-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d sleep.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
add_test(NAME sleep-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d sleep.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/sleep.test.shadow.config.xml)
