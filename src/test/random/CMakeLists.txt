## build the test as a dynamic executable that plugs into shadow
add_executable(shadow-plugin-test-random test_random.c ../test_common.c)

## register the tests
add_test(NAME random COMMAND shadow-plugin-test-random)

add_test(
	NAME random-shadow-ptrace
	COMMAND sh -c "\
        ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=ptrace -l debug -d random.shadow-ptrace.data - \
	"
)
add_test(
	NAME random-shadow-preload
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=preload -l debug -d random.shadow-preload.data - \
	"
)
