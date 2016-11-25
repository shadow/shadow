##
## build the preload libraries
##

## this one has no constructor, preloads time() and does dlsym() lookup there
add_library(test-preload-interpose SHARED shd-test-preload-interpose1.c)
target_link_libraries(test-preload-interpose ${DL_LIBRARIES})

## this one has a constructor in which it does a dlsym() lookup
add_library(test-preload-interpose-constructor SHARED shd-test-preload-interpose1.c shd-test-preload-interpose-constructor.c)
target_link_libraries(test-preload-interpose-constructor ${DL_LIBRARIES})

## this one meant for chaining preloads
add_library(test-preload-interpose2 SHARED shd-test-preload-interpose2.c)
target_link_libraries(test-preload-interpose2 ${DL_LIBRARIES})

## define time and return a constant
add_library(test-preload-lib SHARED shd-test-preload-lib.c)

## different test cases in library form
add_library(test-preload-lib-run1 SHARED shd-test-preload-run1.c)
add_library(test-preload-lib-run2 SHARED shd-test-preload-run2.c)

##
## first build a version that uses the regular glibc loader
##

## basic preload test from an exe and symbol lookup from constructor

add_executable(test-preload-exe1 shd-test-preload-main.c shd-test-preload-run1.c)
target_link_libraries(test-preload-exe1 test-preload-lib)

add_test(NAME test-preload-glibc-exe COMMAND test-preload-exe1)
SET_TESTS_PROPERTIES(test-preload-glibc-exe
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

add_test(NAME test-preload-glibc-exe-constructor COMMAND test-preload-exe1)
SET_TESTS_PROPERTIES(test-preload-glibc-exe-constructor
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")

## another version of the test for chaining

add_executable(test-preload-exe2 shd-test-preload-main.c shd-test-preload-run2.c)
target_link_libraries(test-preload-exe2 test-preload-lib)

add_test(NAME test-preload-glibc-exe-chain COMMAND test-preload-exe2)
SET_TESTS_PROPERTIES(test-preload-glibc-exe-chain
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

## test preloading functions called from a library

add_executable(test-preload-exe-run1 shd-test-preload-main.c)
target_link_libraries(test-preload-exe-run1 test-preload-lib-run1 test-preload-lib)

add_test(NAME test-preload-glibc-lib COMMAND test-preload-exe-run1)
SET_TESTS_PROPERTIES(test-preload-glibc-lib
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

add_test(NAME test-preload-glibc-lib-constructor COMMAND test-preload-exe-run1)
SET_TESTS_PROPERTIES(test-preload-glibc-lib-constructor
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")

add_executable(test-preload-exe-run2 shd-test-preload-main.c)
target_link_libraries(test-preload-exe-run2 test-preload-lib-run2 test-preload-lib)

add_test(NAME test-preload-glibc-lib-chain COMMAND test-preload-exe-run2)
SET_TESTS_PROPERTIES(test-preload-glibc-lib-chain
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

##
## now build a version that uses the elf loader
##

## basic preload test from an exe and symbol lookup from constructor

add_executable(test-preload-elf-exe1 shd-test-preload-main.c shd-test-preload-run1.c)
target_link_libraries(test-preload-elf-exe1 test-preload-lib)
set_target_properties(test-preload-elf-exe1 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf-exe COMMAND test-preload-elf-exe1)
SET_TESTS_PROPERTIES(test-preload-elf-exe
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

add_test(NAME test-preload-elf-exe-constructor COMMAND test-preload-elf-exe1)
SET_TESTS_PROPERTIES(test-preload-elf-exe-constructor
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")

## another version of the test for chaining

add_executable(test-preload-elf-exe2 shd-test-preload-main.c shd-test-preload-run2.c)
target_link_libraries(test-preload-elf-exe2 test-preload-lib)
set_target_properties(test-preload-elf-exe2 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf-exe-chain COMMAND test-preload-elf-exe2)
SET_TESTS_PROPERTIES(test-preload-elf-exe-chain
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

## test preloading functions called from a library

add_executable(test-preload-elf-exe-run1 shd-test-preload-main.c)
target_link_libraries(test-preload-elf-exe-run1 test-preload-lib-run1 test-preload-lib)
set_target_properties(test-preload-elf-exe-run1 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf-lib COMMAND test-preload-exe-run1)
SET_TESTS_PROPERTIES(test-preload-elf-lib
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

add_test(NAME test-preload-elf-lib-constructor COMMAND test-preload-exe-run1)
SET_TESTS_PROPERTIES(test-preload-elf-lib-constructor
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose-constructor.so")

add_executable(test-preload-elf-exe-run2 shd-test-preload-main.c)
target_link_libraries(test-preload-elf-exe-run2 test-preload-lib-run2 test-preload-lib)
set_target_properties(test-preload-elf-exe-run2 PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf-lib-chain COMMAND test-preload-exe-run2)
SET_TESTS_PROPERTIES(test-preload-elf-lib-chain
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose2.so")

##
## test preloading functions called from a dynamically loaded plugin
##

## glibc loader

# note - in order for this to work correctly in glibc, you must link to the interpose library
# this requirement is not ideal - the loaded plugin should not care about preloading
add_library(test-preload-dl-glibc-run1-lib SHARED shd-test-preload-run1.c)
target_link_libraries(test-preload-dl-glibc-run1-lib test-preload-interpose test-preload-lib)

add_executable(test-preload-dl-glibc-run1-exe shd-test-preload-dl-main.c)
target_link_libraries(test-preload-dl-glibc-run1-exe ${DL_LIBRARIES})

add_test(NAME test-preload-glibc-dl COMMAND test-preload-dl-glibc-run1-exe ${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-dl-glibc-run1-lib.so)
SET_TESTS_PROPERTIES(test-preload-glibc-dl
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

## elf loader

add_library(test-preload-dl-elf-run1-lib SHARED shd-test-preload-run1.c)
target_link_libraries(test-preload-dl-elf-run1-lib test-preload-lib)

# note - in order for this to work in elf loader, you must link to the library that
# is also linked in the dynamically loaded plugin
# this requirement is not ideal - the executable should not care about the plugins library dependencies
add_executable(test-preload-dl-elf-run1-exe shd-test-preload-dl-main.c)
target_link_libraries(test-preload-dl-elf-run1-exe test-preload-lib ${DL_LIBRARIES})
set_target_properties(test-preload-dl-elf-run1-exe PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

add_test(NAME test-preload-elf-dl COMMAND test-preload-exe-elf-dl-run1 ${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-dl-run1.so)
SET_TESTS_PROPERTIES(test-preload-elf-dl
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so")

## shadow

## shadow itself already dynamically loads the plugin, so just combine main() and run_test()
add_library(shadow-plugin-test-preload SHARED shd-test-preload-plugin-main.c shd-test-preload-run1.c)
target_link_libraries(shadow-plugin-test-preload test-preload-lib)

## register a shadow test
add_test(NAME test-preload-shadow-dl
         COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug ${CMAKE_CURRENT_SOURCE_DIR}/preload.test.shadow.config.xml)
SET_TESTS_PROPERTIES(test-preload-shadow-dl
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libtest-preload-interpose.so:${CMAKE_BINARY_DIR}/src/preload/libshadow-interpose.so;LD_STATIC_TLS_EXTRA=10240000")

