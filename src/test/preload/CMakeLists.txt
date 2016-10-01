## build the preload library
add_library(shadow-test-preload-lib SHARED shd-test-preload-lib.c)
target_link_libraries(shadow-test-preload-lib ${DL_LIBRARIES})

## build test executable
add_executable(shadow-test-preload shd-test-preload.c)

## register the test
add_test(NAME test-preload 
         COMMAND shadow-test-preload)
SET_TESTS_PROPERTIES(test-preload
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libshadow-test-preload-lib.so")

## now build an executable that uses the elf loader

## build elf-loader version
add_executable(shadow-test-preload-elf shd-test-preload.c)
link_directories(${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(shadow-test-preload-elf PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf 
         COMMAND shadow-test-preload-elf)
SET_TESTS_PROPERTIES(test-preload-elf
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libshadow-test-preload-lib.so")
    