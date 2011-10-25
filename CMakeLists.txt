## (c)2010-2011 Scallion (Shadow Plug-in running Tor) Rob Jansen jansen@cs.umn.edu

project(scallion C)
cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

## set paths and build parameters
set(VERSION_MAJOR 2)
set(VERSION_MINOR 0)
set(VERSION_BUILD 0)
set(PACKAGE_STRING "Scallion ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD}")

# check for unix environment
if(NOT UNIX)
	message(FATAL_ERROR "Scallion requires a UNIX-like environment.")
endif()

## disallow in-source build
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_BUILD_PREFIX}" is_in_source_build)
if(is_in_source_build)
	message(FATAL_ERROR "scallion requires an out of source build. Please create a separate build directory and run 'cmake path_to_source [options]' there.")
endif()

## setup scallion options
option(SCALLION_DEBUG "turn on debugging for verbose program output (default: OFF)" OFF)

## display selected user options
MESSAGE(STATUS)
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS "Current settings: (change with '$ cmake -D<OPTION>=<ON|OFF>')")
MESSAGE(STATUS "SCALLION_DEBUG=${SHADOW_DEBUG}")
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS)

## now handle the options
if(SCALLION_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Debug enabled.")
    set(CMAKE_BUILD_TYPE Debug)
    add_definitions(-DDEBUG)
else(SCALLION_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Release enabled.")
    set(CMAKE_BUILD_TYPE Release)
endif(SCALLION_DEBUG STREQUAL ON)

## get general includes
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)

## run system-wide tests
include (TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

## enable our custom modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

## user-defined includes
foreach(include ${CMAKE_EXTRA_INCLUDES})
    include_directories(${include})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${include}")
endforeach(include)

foreach(library ${CMAKE_EXTRA_LIBRARIES})
    link_directories(${library})
endforeach(library)

## check for dependencies, using built-in cmake files to help us find the libs
## these are in /usr/share/cmake/Modules/Findxxx.cmake
find_package(ZLIB REQUIRED)

## other libs dont have a Findxxx.cmake, so we check on our own (./cmake)
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(OPENSSL REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(GLIB REQUIRED)
find_package(TOR REQUIRED)
find_package(SHADOW REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${EVENT2_INCLUDES} ${OPENSSL_INCLUDES} ${TOR_INCLUDES} ${SHADOW_INCLUDES})

## compile options
add_definitions(-Wall --std=c99 -fPIC -g -D_GNU_SOURCE)

## import shadow libs
#INCLUDE(${CMAKE_INSTALL_PREFIX}/share/shadow/shadow-externals.cmake)

## build source
add_subdirectory(src)

install(FILES build/tor-0.2.3.5-alpha/src/config/geoip build/50KiB.urnd build/320KiB.urnd build/1MiB.urnd build/5MiB.urnd build/GeoLiteCity.dat DESTINATION share/shadow/scallion)
install(DIRECTORY resource/ DESTINATION share/shadow/scallion PATTERN "config/*")
