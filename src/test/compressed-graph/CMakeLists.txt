add_custom_target(compressed-graph-compression ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy
                    ${CMAKE_CURRENT_SOURCE_DIR}/graph-compressed.gml
                    ${CMAKE_CURRENT_BINARY_DIR}/graph-compressed.gml
                  COMMAND xz
                    --force
                    ${CMAKE_CURRENT_BINARY_DIR}/graph-compressed.gml)

add_shadow_tests(BASENAME compressed-graph)
