add_custom_target(tor-minimal-shadow-conf ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_SOURCE_DIR}/src/test/tor/minimal/conf
                    ${CMAKE_CURRENT_BINARY_DIR}/conf
                  # older tor versions don't support the AuthDirTestReachability torrc option,
                  # so comment out those lines if not supported
                  COMMAND /bin/bash -c
                    "(~/.shadow/bin/tor --list-torrc-options | grep 'AuthDirTestReachability') \
                    || find ${CMAKE_CURRENT_BINARY_DIR}/conf -type f -print0 \
                      | xargs -0 sed -i '/AuthDirTestReachability/s/^/#/g'"
                  VERBATIM)

add_custom_target(tor-minimal-shadow-data-template ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy_directory
                    ${CMAKE_SOURCE_DIR}/src/test/tor/minimal/shadow.data.template
                    ${CMAKE_CURRENT_BINARY_DIR}/shadow.data.template
                  COMMAND chmod
                    0700
                    ${CMAKE_CURRENT_BINARY_DIR}/shadow.data.template/hosts/hiddenserver/hs)

# we cannot add other methods since the Shadow config file requires the data
# directory to be hard-coded as 'tor-minimal-shadow-ptrace.data'

add_shadow_tests(BASENAME tor-minimal
                 METHODS ptrace
                 LOGLEVEL info
                 ARGS
                   --pin-cpus
                   --workers 2
                   --data-template "shadow.data.template"
                 PROPERTIES
                   TIMEOUT 180
                   LABELS tor
                   CONFIGURATIONS tor
                   RUN_SERIAL TRUE)
