## build rpth and elf-loader
add_subdirectory(external)

# Outside of external, inclusion of headers within the project should be
# relative to this directory.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

## build the core simulator
add_subdirectory(main)

## build the library that will intercept system calls using LD_PRELOAD
add_subdirectory(preload)

if(SHADOW_TEST STREQUAL ON)
    add_subdirectory(test)
endif(SHADOW_TEST STREQUAL ON)
