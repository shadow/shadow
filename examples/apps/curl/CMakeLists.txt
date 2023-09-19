file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME curl-example
  COMMAND bash run.sh ${CMAKE_BINARY_DIR}/src/main
  CONFIGURATIONS extra
)

set_property(TEST curl-example PROPERTY LABELS shadow example)
