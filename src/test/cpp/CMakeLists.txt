set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=gnu++11")

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-cpp test_cpp.cpp)

## register the tests
add_test(NAME cpp COMMAND shadow-plugin-test-cpp)
add_test(NAME cpp-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d cpp.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/cpp.test.shadow.config.xml)
