# Check if Shadow can read its configuration from stdin
add_executable(test-config-read-from-stdin test_config.c)

add_test(
        NAME config-read-from-stdin
        COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d convert.shadow.data -"
)
