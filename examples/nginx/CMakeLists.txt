file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
       ${CMAKE_CURRENT_SOURCE_DIR}/shadow.yaml
       ${CMAKE_CURRENT_SOURCE_DIR}/nginx.conf
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_test(
  NAME nginx-example
  COMMAND bash run.sh ${CMAKE_BINARY_DIR}/src/main
  CONFIGURATIONS extra
)

set_property(TEST nginx-example PROPERTY LABELS shadow example)
