## build the preload library
add_library(test-preload-lib SHARED shd-test-preload-lib.c)
target_link_libraries(test-preload-lib ${DL_LIBRARIES})

##
## first build a version that uses the regular loader
##

## build test executable
add_executable(test-preload-exe shd-test-preload.c)

## register the test
add_test(NAME test-preload COMMAND test-preload-exe)
SET_TESTS_PROPERTIES(test-preload
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-lib.so")

##
## now build a version that uses the elf loader
##

## build elf-loader version
add_executable(test-preload-elf-exe shd-test-preload.c)
#link_directories(${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(test-preload-elf-exe PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf COMMAND test-preload-elf-exe)
SET_TESTS_PROPERTIES(test-preload-elf
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-lib.so")

##
## now test inside of shadow itself
##

add_library(shadow-plugin-test-preload SHARED shd-test-preload.c)

## register a shadow test
add_test(NAME test-preload-shadow
         COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/preload.test.shadow.config.xml)
SET_TESTS_PROPERTIES(test-preload-shadow
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-lib.so")
