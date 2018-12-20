## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-determinism shd-test-determinism.c)

## We need to run twice to make sure the 'random' output is the same both times
add_test(NAME determinism1-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -l debug -s 1 -d determinism1.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/determinism.test.shadow.config.xml)
add_test(NAME determinism2-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -w 2 -l debug -s 1 -d determinism2.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/determinism.test.shadow.config.xml)

## now compare the output
add_test(NAME determinism-shadow-compare COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_determinism_compare.cmake)
## make sure the tests that produce output finish before we compare the output
set_tests_properties(determinism-shadow-compare PROPERTIES DEPENDS "determinism1-shadow;determinism2-shadow")
