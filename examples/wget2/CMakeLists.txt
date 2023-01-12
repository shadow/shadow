file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME wget2-example
  COMMAND bash run.sh ${CMAKE_BINARY_DIR}/src/main
  CONFIGURATIONS extra
)

set_property(TEST wget2-example PROPERTY LABELS shadow example)
