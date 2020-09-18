include_directories(${GLIB_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test_clone test_clone.c)

## if the test needs any libraries, link them here
target_compile_options(test_clone PUBLIC "-pthread")

target_link_libraries(test_clone ${GLIB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT})

## register the tests
add_test(NAME clone COMMAND test_clone)

add_test(
	NAME clone-shadow-ptrace
	COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/clone_test_config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d clone.shadow_ptrace.data - \
	"
)
