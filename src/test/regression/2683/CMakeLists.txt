file(COPY
       ${CMAKE_CURRENT_SOURCE_DIR}/test.py
     DESTINATION
       ${CMAKE_CURRENT_BINARY_DIR})

add_shadow_tests(BASENAME regression_2683)
add_linux_tests(BASENAME regression_2683 COMMAND sh -c "python3 -u test.py 127.0.0.1")
