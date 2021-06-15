set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=gnu++11")
add_executable(test-cpp test_cpp.cpp)
add_linux_tests(BASENAME cpp COMMAND test-cpp)
add_shadow_tests(BASENAME cpp METHODS hybrid ptrace preload)