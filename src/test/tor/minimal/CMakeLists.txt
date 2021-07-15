add_custom_target(tor-minimal-shadow-conf ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/conf
                    ${CMAKE_CURRENT_BINARY_DIR}/conf
                  # older tor versions don't support the AuthDirTestReachability torrc option,
                  # so comment out those lines if not supported
                  COMMAND /bin/bash -c
                    "(~/.local/bin/tor --list-torrc-options | grep -q 'AuthDirTestReachability') \
                    || find ${CMAKE_CURRENT_BINARY_DIR}/conf -type f -print0 \
                      | xargs -0 sed -i '/AuthDirTestReachability/s/^/#/g'"
                  VERBATIM)

add_custom_target(tor-minimal-shadow-data-template ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/shadow.data.template
                    ${CMAKE_CURRENT_BINARY_DIR}/shadow.data.template
                  COMMAND chmod
                    0700
                    ${CMAKE_CURRENT_BINARY_DIR}/shadow.data.template/hosts/hiddenserver/hs)

add_shadow_tests(BASENAME tor-minimal
                 LOGLEVEL info
                 ARGS
                   --use-cpu-pinning true
                   --parallelism 2
                   --template-directory "shadow.data.template"
                 POST_CMD "${CMAKE_CURRENT_SOURCE_DIR}/verify.sh"
                 PROPERTIES
                   TIMEOUT 180
                   RUN_SERIAL TRUE
                   LABELS tor
                   CONFIGURATIONS extra)
