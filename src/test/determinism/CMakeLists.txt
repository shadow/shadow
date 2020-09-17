## TEST 1 (Basic tests)

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-determinism test_determinism.c)
target_link_libraries(shadow-plugin-test-determinism ${CMAKE_THREAD_LIBS_INIT})

## We need to run twice to make sure the 'random' output is the same both times
add_test(
	NAME determinism1a-shadow
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -s 1 -d determinism1a.shadow.data - \
	"
)
add_test(
	NAME determinism1b-shadow
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml --output - \
	| ${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -s 1 -d determinism1b.shadow.data \
	"
)

## now compare the output
add_test(NAME determinism1-shadow-compare COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism1_compare.cmake)
## make sure the tests that produce output finish before we compare the output
set_tests_properties(determinism1-shadow-compare PROPERTIES DEPENDS "determinism1a-shadow;determinism1b-shadow")

## TEST 2 (Extended packet tests)

## TODO
## We could also split the shadow.log file into a separate file for each host
## and then grep each file for "packet_add", and do diffs on the output of that.
## That way we could check the order of every packet event at every processing point.

## now let's run a phold test and compare the order of packet events
add_test(
	NAME determinism2a-shadow
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml --output - \
	${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -l debug -s 1 -d determinism2a.shadow.data - \
	"
)
add_test(
	NAME determinism2b-shadow
	COMMAND sh -c "\
	${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml --output - \
	${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -l debug -s 1 -d determinism2b.shadow.data \
	"
)

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

## now compare the output
add_test(NAME determinism2-shadow-compare COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism2_compare.cmake)
## make sure the tests that produce output finish before we compare the output
set_tests_properties(determinism2-shadow-compare PROPERTIES DEPENDS "determinism2a-shadow;determinism2b-shadow;phold-shadow")
