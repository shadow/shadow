add_executable(test-poll test_poll.c)
target_link_libraries(test-poll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME poll COMMAND test-poll)
# FIXME add_test(
# FIXME     NAME poll-shadow-ptrace
# FIXME     COMMAND sh -c "\
# FIXME     ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
# FIXME     ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d poll.shadow-ptrace.data - \
# FIXME     "
# FIXME )
# FIXME add_test(
# FIXME     NAME poll-shadow-preload
# FIXME     COMMAND sh -c "\
# FIXME     ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/poll.test.shadow.config.yaml --output - \
# FIXME     | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d poll.shadow-preload.data - \
# FIXME     "
# FIXME )
