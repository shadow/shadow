add_executable(test-config-convert test_config.c)

# Convert an XML file to YAML
add_test(
    NAME convert-config
    COMMAND python3 ${CMAKE_SOURCE_DIR}/src/tools/convert_legacy_config.py --output shadow.generated.yaml ${CMAKE_CURRENT_SOURCE_DIR}/shadow.original.xml
    CONFIGURATIONS extra
)

# Launch Shadow to ensure that the previously generated YAML is correct
add_test(
    NAME convert-config-run-shadow
    COMMAND sh -c "\
    rm -rf shadow.data \
    && ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d shadow.data shadow.generated.yaml \
    "
    CONFIGURATIONS extra
)
set_tests_properties(convert-config-run-shadow PROPERTIES DEPENDS "convert-config")

# Ensure the format of the conversion is as expected
add_test(
    NAME convert-config-check-format
    COMMAND diff -U 5 ${CMAKE_CURRENT_SOURCE_DIR}/shadow.expected.yaml shadow.generated.yaml
    CONFIGURATIONS extra
)
set_tests_properties(convert-config-check-format PROPERTIES DEPENDS "convert-config")

# Convert a GraphML file to GML
add_test(
    NAME convert-topology
    COMMAND sh -c "\
    ${CMAKE_SOURCE_DIR}/src/tools/convert_legacy_topology.py \
    ${CMAKE_CURRENT_SOURCE_DIR}/topology.original.xml \
    > topology.generated.gml \
    "
    CONFIGURATIONS extra
)

# Ensure the format of the conversion is as expected
add_test(
    NAME convert-topology-check-format
    COMMAND diff -U 5 ${CMAKE_CURRENT_SOURCE_DIR}/topology.expected.gml topology.generated.gml
    CONFIGURATIONS extra
)
set_tests_properties(convert-topology-check-format PROPERTIES DEPENDS "convert-topology")
