include_directories(${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-phold test_phold.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-phold ${M_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

## register the tests
## dont run with debug logging because it causes the test case to take too long
add_test(NAME phold-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -d phold.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
add_test(NAME phold-threaded-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -d phold-threaded.shadow.data -w 2 ${CMAKE_CURRENT_SOURCE_DIR}/phold.test.shadow.config.xml)
