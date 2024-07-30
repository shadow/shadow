file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME jetty-example
  COMMAND bash run.sh ${CMAKE_BINARY_DIR}/src/main
  CONFIGURATIONS extra
)

set_property(TEST jetty-example PROPERTY LABELS shadow example)
