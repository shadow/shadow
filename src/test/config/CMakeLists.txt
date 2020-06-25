include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

add_shadow_exe(test-config test_config.c)

add_test(
        NAME config-convert-xml2yaml
        COMMAND /usr/bin/env python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py xml2yaml --output test.shadow.config.yaml ${CMAKE_CURRENT_SOURCE_DIR}/shadow.config.xml
)

add_test(
        NAME config-convert-yaml2xml
        COMMAND /usr/bin/env python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py yaml2xml  --output test.shadow.config.xml test.shadow.config.yaml
)
set_tests_properties(config-convert-yaml2xml PROPERTIES DEPENDS "config-convert-xml2yaml")

add_test(
        NAME config-convert-udp-shadow
        COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d convert.shadow.data test.shadow.config.xml
)
set_tests_properties(config-convert-udp-shadow PROPERTIES DEPENDS "config-convert-yaml2xml")
