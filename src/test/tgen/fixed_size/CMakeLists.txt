# The same tgen server config is used for all tests.
add_custom_target(tgen-size-server-setup ALL
                COMMAND ${CMAKE_COMMAND}
                  -E copy
                  ${CMAKE_CURRENT_SOURCE_DIR}/server.graphml
                  ${CMAKE_CURRENT_BINARY_DIR}/server.graphml)

# Loop through our client tgen configs
foreach(client 1stream_1b_1000x 1stream_1kib_100x 1stream_1mib_10x 10streams_1mib_10x 100streams_1kib_100x 1000streams_1b_1000x)
  # Copy the client tgen config to the bin dir
  add_custom_target(tgen-size-${client}-setup ALL
                  COMMAND ${CMAKE_COMMAND}
                    -E copy
                    ${CMAKE_CURRENT_SOURCE_DIR}/client.${client}.graphml
                    ${CMAKE_CURRENT_BINARY_DIR}/client.${client}.graphml)

  # Loop through the shadow configs, each of which define a unique network
  foreach(network 1mbit_300ms 10mbit_200ms 100mbit_100ms 1gbit_10ms)
    # Copy the shadow config to the bin dir, write the tgen configf filename into the yaml
    add_custom_target(tgen-size-${network}-${client}-setup ALL
                    COMMAND ${CMAKE_COMMAND}
                      -E copy
                      ${CMAKE_CURRENT_SOURCE_DIR}/${network}.yaml
                      ${CMAKE_CURRENT_BINARY_DIR}/${network}-${client}.yaml
                    COMMAND sed
                      -i 's/XXXCLIENTCONFXXX/client.${client}.graphml/g'
                      ${CMAKE_CURRENT_BINARY_DIR}/${network}-${client}.yaml)

    # Add the test, after which we run a script to check performance
    add_shadow_tests(BASENAME tgen-size-${network}-${client}
                    SHADOW_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/${network}-${client}.yaml
                    LOGLEVEL info
                    # tgen server runs forever, which is not a failure
                    CHECK_RETVAL false
                    POST_CMD "${CMAKE_CURRENT_SOURCE_DIR}/verify.sh ${network} ${client}"
                    PROPERTIES
                      TIMEOUT 60
                      LABELS tgen
                      CONFIGURATIONS extra)
  endforeach(network)
endforeach(client)
