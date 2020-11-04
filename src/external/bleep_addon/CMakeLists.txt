
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

add_library(bleep_addon SHARED bleep_addon.cpp)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
message(${CMAKE_CURRENT_SOURCE_DIR}/.libs)
install(TARGETS bleep_addon
        DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/.libs")