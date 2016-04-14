## test: a benchmark and functional test plug-in for Shadow
project(shadow-plugin-test)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

add_definitions(-D_GNU_SOURCE)

find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_cflags("-fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

## create and install the shared library that plugs into shadow
add_shadow_plugin(shadow-plugin-test shd-test.c)
target_link_libraries(shadow-plugin-test ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-test DESTINATION plugins)
