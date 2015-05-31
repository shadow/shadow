## build the core simulator
add_subdirectory(main)

## build the library that will intercept system calls using LD_PRELOAD
add_subdirectory(preload)

## build plug-in libraries and other components
add_subdirectory(plugin)

## build test if enabled
if(SHADOW_TEST STREQUAL ON)
    message(STATUS "SHADOW_TEST enabled")
    add_subdirectory(${CMAKE_SOURCE_DIR}/test/)
endif(SHADOW_TEST STREQUAL ON)