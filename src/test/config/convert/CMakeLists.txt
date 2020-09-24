# Test the convert shadow config files between XML and YAML
# First, convert a file from XML to YAML, then this file from YAML to XML
# Second, ensure this XML file is understandable by Shadow by running it with the Shadow executable
# Third, make a diff between the files generated and the example files

add_executable(test-config-convert test_config.c)

# Convert a XML file to YAML
## To a file
add_test(
    NAME config-convert-xml2yaml
    COMMAND python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py xml2yaml --output test.shadow.config.yaml ${CMAKE_CURRENT_SOURCE_DIR}/shadow.config.xml
)
## To stdout
add_test(
    NAME config-convert-xml2yaml-stdout
    COMMAND sh -c "\
	python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py xml2yaml --output - ${CMAKE_CURRENT_SOURCE_DIR}/shadow.config.xml \
	| diff ${CMAKE_CURRENT_SOURCE_DIR}/xml2yaml.yaml - \
	"
)

# Convert the YAML previously generated in XML (we need to wait its creation)
## To a file
add_test(
        NAME config-convert-yaml2xml
        COMMAND python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py yaml2xml --output test.shadow.config.xml test.shadow.config.yaml
)
set_tests_properties(config-convert-yaml2xml PROPERTIES DEPENDS "config-convert-xml2yaml")
## To stdout
add_test(
        NAME config-convert-yaml2xml-stdout
        COMMAND sh -c "\
	python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py yaml2xml --output - test.shadow.config.yaml \
	| diff ${CMAKE_CURRENT_SOURCE_DIR}/yaml2xml.xml - \
	"
)
set_tests_properties(config-convert-yaml2xml-stdout PROPERTIES DEPENDS "config-convert-xml2yaml")


# Launch Shadow to ensure that the XML previously generated from the YAML is correct
add_test(
        NAME config-convert-shadow
        COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d convert.shadow.data test.shadow.config.xml
)
set_tests_properties(config-convert-shadow PROPERTIES DEPENDS "config-convert-yaml2xml")

# Ensure the format of the convertion is as expected from the XML to YAML convertion
add_test(
        NAME config-convert-xml2yaml-format
        COMMAND diff ${CMAKE_CURRENT_SOURCE_DIR}/xml2yaml.yaml test.shadow.config.yaml
)
set_tests_properties(config-convert-xml2yaml-format PROPERTIES DEPENDS "config-convert-xml2yaml")

# Ensure the format of the convertion is as expected from the YAML to XML convertion
add_test(
        NAME config-convert-yaml2xml-format
        COMMAND diff ${CMAKE_CURRENT_SOURCE_DIR}/yaml2xml.xml test.shadow.config.xml
)
set_tests_properties(config-convert-yaml2xml-format PROPERTIES DEPENDS "config-convert-yaml2xml")
