file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/show.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
       ${CMAKE_CURRENT_SOURCE_DIR}/conf
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.data.template
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME tor-example
  COMMAND bash -c "./run.sh ${CMAKE_BINARY_DIR}/src/main && ./show.sh"
  CONFIGURATIONS extra
)

set_property(TEST tor-example PROPERTY LABELS shadow example)
set_property(TEST tor-example PROPERTY TIMEOUT 60)
