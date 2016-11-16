##
## build the preload libraries
##

## this one has no constructor, preloads time() and does dlsym() lookup there
add_library(test-preload-interpose SHARED shd-test-preload-interpose.c)
target_link_libraries(test-preload-interpose ${DL_LIBRARIES})

## this one meant for chaining preloads
add_library(test-preload-interpose2 SHARED shd-test-preload-interpose2.c)
target_link_libraries(test-preload-interpose2 ${DL_LIBRARIES})

## this one has a constructor in which it does a dlsym() lookup
add_library(test-preload-interpose-constructor SHARED shd-test-preload-interpose.c shd-test-preload-interpose-constructor.c)
target_link_libraries(test-preload-interpose-constructor ${DL_LIBRARIES})

## define time and return a constant
add_library(test-preload-lib SHARED shd-test-preload-lib.c)


##
## first build a version that uses the regular loader
##

## build test executables
add_executable(test-preload-exe1 shd-test-preload-main.c shd-test-preload-run1.c)
target_link_libraries(test-preload-exe1 test-preload-lib)

add_executable(test-preload-exe2 shd-test-preload-main.c shd-test-preload-run2.c)
target_link_libraries(test-preload-exe2 test-preload-lib)

## register the tests
add_test(NAME test-preload COMMAND test-preload-exe1)
SET_TESTS_PROPERTIES(test-preload
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

## version for chaining
add_test(NAME test-preload-chain COMMAND test-preload-exe2)
SET_TESTS_PROPERTIES(test-preload-chain
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

## the version with the constructor
add_test(NAME test-preload-constructor COMMAND test-preload-exe1)
SET_TESTS_PROPERTIES(test-preload-constructor
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")


##
## now build a version that uses the elf loader
##

## build elf-loader version executable
add_executable(test-preload-elf-exe1 shd-test-preload-main.c shd-test-preload-run1.c)
target_link_libraries(test-preload-elf-exe1 test-preload-lib)
set_target_properties(test-preload-elf-exe1 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_executable(test-preload-elf-exe2 shd-test-preload-main.c shd-test-preload-run2.c)
target_link_libraries(test-preload-elf-exe2 test-preload-lib)
set_target_properties(test-preload-elf-exe2 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

## register the tests
add_test(NAME test-preload-elf COMMAND test-preload-elf-exe1)
SET_TESTS_PROPERTIES(test-preload-elf
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

## version for chaining
add_test(NAME test-preload-chain-elf COMMAND test-preload-elf-exe2)
SET_TESTS_PROPERTIES(test-preload-chain-elf
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

## the version with the constructor
add_test(NAME test-preload-constructor-elf COMMAND test-preload-elf-exe1)
SET_TESTS_PROPERTIES(test-preload-constructor-elf
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")


##
## now test inside of shadow itself
##

#add_library(shadow-plugin-test-preload SHARED shd-test-preload.c)
#target_link_libraries(shadow-plugin-test-preload test-preload-lib)

## register a shadow test
#add_test(NAME test-preload-shadow
#         COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/preload.test.shadow.config.xml)
#SET_TESTS_PROPERTIES(test-preload-shadow
#    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_BINARY_DIR}/src/preload/libshadow-interpose.so;LD_STATIC_TLS_EXTRA=10240000")

## register the constructor version
#add_test(NAME test-preload-shadow-constructor
#         COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/preload.test.shadow.config.xml)
#SET_TESTS_PROPERTIES(test-preload-shadow-constructor
#    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so:${CMAKE_BINARY_DIR}/src/preload/libshadow-interpose.so;LD_STATIC_TLS_EXTRA=10240000")

