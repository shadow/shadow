add_executable(test-poll test_poll.c)
target_link_libraries(test-poll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME poll COMMAND test-poll)
add_test(NAME poll-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d poll.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.xml)
# FIXME add_test(NAME poll-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d poll.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.xml)
