add_custom_target(tor-minimal-shadow-conf ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/conf
                    ${CMAKE_CURRENT_BINARY_DIR}/conf)

add_custom_target(tor-minimal-shadow-data-template ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/shadow.data.template
                    ${CMAKE_CURRENT_BINARY_DIR}/shadow.data.template)

add_shadow_tests(BASENAME tor-minimal
                 LOGLEVEL info
                 ARGS
                   --use-cpu-pinning true
                   --parallelism 2
                   --strace-logging-mode off
                   # Disable to support fork
                   --use-memory-manager false
                   --template-directory "shadow.data.template"
                 POST_CMD "${CMAKE_CURRENT_SOURCE_DIR}/verify.sh"
                 PROPERTIES
                   TIMEOUT 180
                   RUN_SERIAL TRUE
                   LABELS tor
                   CONFIGURATIONS extra)
