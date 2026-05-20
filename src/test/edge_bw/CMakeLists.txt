# Add edge bandwidth limiting integration test
add_shadow_tests(
    BASENAME edge_bw
    LOGLEVEL trace
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/edge_bw.yaml
    ARGS --template-directory ${CMAKE_CURRENT_SOURCE_DIR}
    POST_CMD "bash ${CMAKE_CURRENT_SOURCE_DIR}/verify.sh"
)
