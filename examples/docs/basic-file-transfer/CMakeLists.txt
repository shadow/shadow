file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/show.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME basic-file-transfer-example
  COMMAND bash -c "./run.sh ${CMAKE_BINARY_DIR}/src/main && ./show.sh"
  CONFIGURATIONS extra
)

set_property(TEST basic-file-transfer-example PROPERTY LABELS shadow example)
