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
                 # don't check tor's return value since it will be killed by shadow at the end of
                 # the sim
                 CHECK_RETVAL false
                 ARGS
                   --use-cpu-pinning true
                   --parallelism 2
                   --strace-logging-mode off
                   --template-directory "shadow.data.template"
                 POST_CMD "${CMAKE_CURRENT_SOURCE_DIR}/verify.sh"
                 PROPERTIES
                   TIMEOUT 180
                   RUN_SERIAL TRUE
                   LABELS tor
                   CONFIGURATIONS extra)
