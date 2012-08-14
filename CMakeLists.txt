## (c) 2010-2012 Shadow, Rob Jansen jansen@cs.umn.edu

project(Shadow C)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

## set paths and build parameters
set(Shadow_VERSION_MAJOR 1)
set(Shadow_VERSION_MINOR 4)
set(Shadow_VERSION_BUILD 0)
set(Shadow_VERSION_SUFFIX "-dev")

#from scallion plug-in, not sure if we need it...
#set(PACKAGE_STRING "Scallion ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD}")

## custom output paths
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

## output locations
set(SHADOW_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(SHADOW_TEST_DIR ${CMAKE_SOURCE_DIR}/test)

## check for unix environment (CMAKE_SYSTEM_NAME == "Linux")
if(NOT UNIX)
	message(FATAL_ERROR "Shadow requires a UNIX-like environment.")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Shadow requires a Linux environment.")
endif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")

## disallow in-source build
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_BUILD_PREFIX}" buildInSource)
if(buildInSource)
	message(FATAL_ERROR "Shadow requires an out-of-source build. Please create a separate build directory and run 'cmake path/to/shadow/src [options]' there.")
endif()

## setup shadow options
option(SHADOW_DEBUG "turn on debugging for verbose program output (default: OFF)" OFF)
option(SHADOW_PROFILE "build with profile settings (default: OFF)" OFF)
option(SHADOW_TEST "build tests (default: OFF)" OFF)
option(SCALLION_DOREFILL "Tor should use refill callbacks (default: OFF)" OFF)
option(SCALLION_TORPATH "path to custom Tor base directory (default: OFF)" OFF)

## display selected user options
MESSAGE(STATUS)
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS "Current settings: (change with '$ cmake -D<OPTION>=<ON|OFF>')")
MESSAGE(STATUS "SHADOW_DEBUG=${SHADOW_DEBUG}")
MESSAGE(STATUS "SHADOW_PROFILE=${SHADOW_PROFILE}")
MESSAGE(STATUS "SHADOW_TEST=${SHADOW_TEST}")
MESSAGE(STATUS "SCALLION_DOREFILL=${SCALLION_DOREFILL}")
MESSAGE(STATUS "SCALLION_TORPATH=${SCALLION_TORPATH}")
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS)

## now handle the options
if(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Debug enabled.")
    set(CMAKE_BUILD_TYPE Debug)
    add_definitions(-DDEBUG ${CMAKE_C_FLAGS_DEBUG} -g)
else(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Release enabled.")
    set(CMAKE_BUILD_TYPE Release)
endif(SHADOW_DEBUG STREQUAL ON)

if(SHADOW_PROFILE STREQUAL ON)
    add_definitions(-DDEBUG ${CMAKE_C_FLAGS_DEBUG} -g -pg)
    set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} -pg)
endif(SHADOW_PROFILE STREQUAL ON)

if(SCALLION_DOREFILL)
    MESSAGE(STATUS "adding -DDOREFILL")
    add_definitions(-DDOREFILL)
endif(SCALLION_DOREFILL)

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

## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## generate config header and make sure its on the include path
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h DESTINATION include/)

## compile options
add_definitions(-Wall --std=c99 -D_SVID_SOURCE -D_XOPEN_SOURCE=600 -fPIC) #-D_GNU_SOURCE

## build source
add_subdirectory(${SHADOW_SOURCE_DIR})

## build test if enabled
if(SHADOW_TEST STREQUAL ON)
    message(STATUS "SHADOW_TEST enabled")
    add_subdirectory(${SHADOW_TEST_DIR})
endif(SHADOW_TEST STREQUAL ON)

## install our 'exported' libs so they can be imported by others
## put the .cmake file for importing in shadow-specific directory
#install(EXPORT shadow-externals DESTINATION share/)

## install resources useful for simulations
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/resource/topology.xml)
    message(STATUS "Decompressing ${CMAKE_SOURCE_DIR}/resource/topology.xml.xz")
    execute_process(COMMAND xz -dk topology.xml.xz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/resource/ RESULT_VARIABLE XZRESULT)
    if(NOT (${XZRESULT} EQUAL 0))
        message(FATAL_ERROR "xz -dk topology.xml.xz failed. Is xz-utils installed and in your path?")
    endif(NOT (${XZRESULT} EQUAL 0))
endif(NOT EXISTS ${CMAKE_SOURCE_DIR}/resource/topology.xml)

install(FILES ${CMAKE_SOURCE_DIR}/resource/topology.xml DESTINATION share/)

## do CPack stuff
add_subdirectory(${CMAKE_SOURCE_DIR}/cpack/)
