find_package(Threads REQUIRED)
include_directories(${RT_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-pthreads shd-test-pthreads.c)

## create and install an executable that can run outside of shadow
add_executable(test-pthreads shd-test-pthreads.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-pthreads ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(test-pthreads ${CMAKE_THREAD_LIBS_INIT})

## register the tests
add_test(NAME test-pthreads COMMAND test-pthreads)
add_test(NAME test-pthreads-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/pthreads.test.shadow.config.xml)
