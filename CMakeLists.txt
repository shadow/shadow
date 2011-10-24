## (c)2010-2011 Shadow, Rob Jansen jansen@cs.umn.edu

project(Shadow C)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

## set paths and build parameters
set(Shadow_VERSION_MAJOR 2)
set(Shadow_VERSION_MINOR 0)
set(Shadow_VERSION_BUILD 0)

## custom output paths
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

## output locations
set(SHADOW_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(SHADOW_TEST_DIR ${CMAKE_SOURCE_DIR}/test)
set(SHADOW_PLUGIN_DIR $ENV{HOME}/.shadow/plugins)

# check for unix environment
if(NOT UNIX)
	message(FATAL_ERROR "Shadow requires a UNIX-like environment.")
endif()

## disallow in-source build
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_BUILD_PREFIX}" buildInSource)
if(buildInSource)
	message(FATAL_ERROR "Shadow requires an out-of-source build. Please create a separate build directory and run 'cmake path/to/shadow/src [options]' there.")
endif()

## setup shadow options
option(SHADOW_DEBUG "turn on debugging for verbose program output (default: OFF)" OFF)
option(SHADOW_TEST "build tests (default: OFF)" OFF)

## display selected user options
MESSAGE(STATUS)
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS "Current settings: (change with '$ cmake -D<OPTION>=<ON|OFF>')")
MESSAGE(STATUS "SHADOW_DEBUG=${SHADOW_DEBUG}")
MESSAGE(STATUS "SHADOW_TEST=${SHADOW_TEST}")
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS)

## now handle the options
if(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Debug enabled.")
    set(CMAKE_BUILD_TYPE Debug)
    add_definitions(-DDEBUG)
else(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Release enabled.")
    set(CMAKE_BUILD_TYPE Release)
endif(SHADOW_DEBUG STREQUAL ON)

## get general includes
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)

## run system-wide tests
include (TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

## enable our custom modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

## additional user-defined include directories
foreach(include ${CMAKE_EXTRA_INCLUDES})
    include_directories(${include})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${include}")
endforeach(include)

## additional user-defined library directories
foreach(library ${CMAKE_EXTRA_LIBRARIES})
    link_directories(${library})
endforeach(library)

## check for dependencies, using built-in cmake files to help us find the libs
## these are in /usr/share/cmake/Modules/Findxxx.cmake
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

## other libs dont have a Findxxx.cmake, so we check on our own (./cmake)
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(GLIB REQUIRED)
find_package(OPENSSL REQUIRED)

## generate config header and make sure its on the include path
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h DESTINATION include/shadow)

## compile options
add_definitions(-Wall --std=c99 -D__USE_BSD -D_SVID_SOURCE -D_XOPEN_SOURCE=600 -fPIC -g)

## build source
add_subdirectory(${SHADOW_SOURCE_DIR})

## build test if enabled
if(SHADOW_TEST STREQUAL ON)
    message(STATUS "SHADOW_TEST enabled")
    add_subdirectory(${SHADOW_TEST_DIR})
endif(SHADOW_TEST STREQUAL ON)

## install our 'exported' libs so they can be imported by others
## put the .cmake file for importing in shadow-specific directory
#install(EXPORT shadow-externals DESTINATION share/shadow)

## install resources useful for simulations
#install(DIRECTORY ${CMAKE_BUILD_PREFIX}/../shadow-resources/
#    DESTINATION share/shadow/resources
#    PATTERN "${CMAKE_BUILD_PREFIX}/../shadow-resources/*")

## do CPack stuff
add_subdirectory(${CMAKE_SOURCE_DIR}/cpack/)
