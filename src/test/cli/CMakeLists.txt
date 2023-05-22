# to update the expected help text, run:
# ./build/src/main/shadow --help > src/test/cli/help-long-expected
# ./build/src/main/shadow -h > src/test/cli/help-short-expected

add_test(NAME cli-long COMMAND sh -c "${CMAKE_BINARY_DIR}/src/main/shadow --help > help-long-current")
add_test(
    NAME cli-long-compare
    COMMAND ${CMAKE_COMMAND} -E compare_files help-long-current ${CMAKE_CURRENT_SOURCE_DIR}/help-long-expected)
set_tests_properties(cli-long-compare PROPERTIES DEPENDS cli-long)

add_test(NAME cli-short COMMAND sh -c "${CMAKE_BINARY_DIR}/src/main/shadow -h > help-short-current")
add_test(
    NAME cli-short-compare
    COMMAND ${CMAKE_COMMAND} -E compare_files help-short-current ${CMAKE_CURRENT_SOURCE_DIR}/help-short-expected)
set_tests_properties(cli-short-compare PROPERTIES DEPENDS cli-short)