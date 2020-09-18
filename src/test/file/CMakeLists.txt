include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

## build the test as a dynamic executable
add_executable(test-file test_file.c)

## register the tests
add_test(NAME file COMMAND test-file)


add_test(
	NAME file-shadow-ptrace
	COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/file.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d file.shadow-ptrace.data - \
	"
)
# FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
add_test(
	NAME file-shadow-preload
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/file.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d file.shadow-preload.data - "
	CONFIGURATIONS ilibc
)

