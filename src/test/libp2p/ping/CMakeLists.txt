add_shadow_tests(BASENAME libp2p-ping
                 SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/ping.yaml"
                 POST_CMD "${CMAKE_CURRENT_SOURCE_DIR}/verify.sh"
                 CONFIGURATIONS extra)
