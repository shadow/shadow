file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/show.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
       ${CMAKE_CURRENT_SOURCE_DIR}/tgen.client.graphml.xml
       ${CMAKE_CURRENT_SOURCE_DIR}/tgen.server.graphml.xml
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME traffic-generation-example
  COMMAND bash -c "./run.sh ${CMAKE_BINARY_DIR}/src/main && ./show.sh"
  CONFIGURATIONS extra
)

set_property(TEST traffic-generation-example PROPERTY LABELS shadow example)
set_property(TEST traffic-generation-example PROPERTY TIMEOUT 60)
