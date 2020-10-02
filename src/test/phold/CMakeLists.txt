include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_executable(test-phold test_phold.c)

## if the test needs any libraries, link them here
target_link_libraries(test-phold ${M_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

## register the tests
## dont run with debug logging because it causes the test case to take too long
add_test(NAME phold-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -d phold.shadow-ptrace.data ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(NAME phold-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -d phold.shadow-preload.data ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml CONFIGURATIONS ilibc)

add_test(NAME phold-threaded-shadow-ptrace COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -d phold-threaded.shadow-ptrace.data -w 2 ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(NAME phold-threaded-shadow-preload COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -d phold-threaded.shadow-preload.data -w 2 ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml CONFIGURATIONS ilibc)

# These tests currently run very slowly in the GitHub CI, which only gives them 2 CPUs.
# Hopefully will be fixed by https://github.com/shadow/shadow/issues/965.
# set_tests_properties(phold-shadow-ptrace phold-shadow-preload phold-threaded-shadow-ptrace phold-threaded-shadow-preload PROPERTIES TIMEOUT 60)
