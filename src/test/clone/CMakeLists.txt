include_directories(${GLIB_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test_clone test_clone.c)

## if the test needs any libraries, link them here
target_compile_options(test_clone PUBLIC "-pthread")

target_link_libraries(test_clone ${GLIB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT})

## register the tests
add_test(NAME test_clone COMMAND test_clone)

add_test(NAME shadow_test_clone_ptrace COMMAND
    ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d
    clone.shadow_clone.data ${CMAKE_CURRENT_SOURCE_DIR}/clone_test_config.xml)
