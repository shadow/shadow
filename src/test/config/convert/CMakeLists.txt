add_executable(test-config-convert test_config.c)

# Convert an XML file to YAML
add_test(
    NAME config-convert
    COMMAND python3 ${CMAKE_SOURCE_DIR}/src/tools/convert_legacy_config.py --output shadow.generated.yaml ${CMAKE_CURRENT_SOURCE_DIR}/shadow.original.xml
)

# Launch Shadow to ensure that the previously generated YAML is correct
add_test(
    NAME config-convert-run-shadow
    COMMAND sh -c "\
    rm -rf shadow.data \
    && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d shadow.data shadow.generated.yaml \
    "
)
set_tests_properties(config-convert-run-shadow PROPERTIES DEPENDS "config-convert")

# Ensure the format of the conversion is as expected
add_test(
    NAME config-convert-check-format
    COMMAND diff -U 5 ${CMAKE_CURRENT_SOURCE_DIR}/shadow.expected.yaml shadow.generated.yaml
)
set_tests_properties(config-convert-check-format PROPERTIES DEPENDS "config-convert")
