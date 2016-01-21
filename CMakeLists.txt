## (c) 2010-2012 Shadow, Rob Jansen jansen@cs.umn.edu

## set build parameters
project(Shadow C)
set(SHADOW_VERSION_MAJOR 1)
set(SHADOW_VERSION_MINOR 11)
set(SHADOW_VERSION_BUILD 1)

## ensure cmake version
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

## ensure unix environment (CMAKE_SYSTEM_NAME == "Linux")
if((NOT UNIX) OR (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux")))
    message(FATAL_ERROR "Shadow requires a Unix/Linux environment.")
endif((NOT UNIX) OR (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux")))

## ensure out-of-source build
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Shadow requires an out-of-source build. Please create a separate build directory and run 'cmake path/to/shadow [options]' there.")
endif(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

## construct info/version strings
set(SHADOW_INFO_STRING_CONF "For more information, visit https://shadow.github.io or https://github.com/shadow")
set(SHADOW_VERSION_STRING_CONF "Shadow")
if(EXISTS ${CMAKE_SOURCE_DIR}/.git)
    ## current git commit version and hash
    EXECUTE_PROCESS(COMMAND "git" "describe" "--long" "--dirty" OUTPUT_VARIABLE DESCRIBE)
    if(DESCRIBE)
        string(REGEX REPLACE "\n" "" DESCRIBE ${DESCRIBE})
        set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} ${DESCRIBE}")
    endif(DESCRIBE)
    
    ## current git commit short date
    EXECUTE_PROCESS(COMMAND "git" "log" "--pretty=format:%ad" "--date=short" "-n"  "1" OUTPUT_VARIABLE SHORTDATE)
    if(SHORTDATE)
        set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} ${SHORTDATE}")
    endif(SHORTDATE)
else()
    set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} v${SHADOW_VERSION_MAJOR}.${SHADOW_VERSION_MINOR}.${SHADOW_VERSION_BUILD}")
endif()

## current build date
EXECUTE_PROCESS(COMMAND "date" "+%d/%m/%Y" OUTPUT_VARIABLE BUILDDATE)
if(BUILDDATE)
    string(REGEX REPLACE "(..)/(..)/(....).*" "\\3-\\2-\\1" BUILDDATE ${BUILDDATE})
    set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} (built ${BUILDDATE})")
endif(BUILDDATE)

macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

## setup shadow options
option(SHADOW_DEBUG "turn on debugging for verbose program output (default: OFF)" OFF)
option(SHADOW_PROFILE "build with profile settings (default: OFF)" OFF)
option(SHADOW_TEST "build tests (default: OFF)" OFF)
option(SHADOW_EXPORT "export service libraries and headers (default: OFF)" OFF)

## display selected user options
MESSAGE(STATUS)
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS "Current settings: (change with '$ cmake -D<OPTION>=<ON|OFF>')")
MESSAGE(STATUS "SHADOW_DEBUG=${SHADOW_DEBUG}")
MESSAGE(STATUS "SHADOW_PROFILE=${SHADOW_PROFILE}")
MESSAGE(STATUS "SHADOW_TEST=${SHADOW_TEST}")
MESSAGE(STATUS "SHADOW_EXPORT=${SHADOW_EXPORT}")
MESSAGE(STATUS "-------------------------------------------------------------------------------")
MESSAGE(STATUS)

## now handle the options, set up our own flags
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
if(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Debug enabled.")
    set(CMAKE_BUILD_TYPE Debug)
    add_definitions(-DDEBUG)
    add_cflags(-g)
else(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Release enabled.")
    set(CMAKE_BUILD_TYPE Release)
    add_definitions(-DNDEBUG)
    #add_cflags(-O3) # causes segfault in Tor
endif(SHADOW_DEBUG STREQUAL ON)

if($ENV{VERBOSE})
    add_definitions(-DVERBOSE)
endif()

if(SHADOW_PROFILE STREQUAL ON)
    add_definitions(-DDEBUG)
    add_cflags(-pg)
    #set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} -pg)
endif(SHADOW_PROFILE STREQUAL ON)

if(SHADOW_EXPORT STREQUAL ON)
    ## the actual work happens in the CMakeLists files in each plug-in directory
    MESSAGE(STATUS "will export Shadow plug-in service libraries and headers")
endif(SHADOW_EXPORT STREQUAL ON)

## get general includes
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(TestBigEndian)

## general tests and configurations
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

## additional user-defined include directories
foreach(include ${CMAKE_EXTRA_INCLUDES})
    include_directories(${include})
    set(CMAKE_MODULE_PATH "${include}" ${CMAKE_MODULE_PATH})
endforeach(include)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})

message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

## additional user-defined library directories
foreach(library ${CMAKE_EXTRA_LIBRARIES})
    link_directories(${library})
endforeach(library)

## generate config header and make sure its on the include path
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/shd-config.h DESTINATION include/)

if(POLICY  CMP0026)
    cmake_policy(SET  CMP0026  OLD)
endif()

## recurse our project tree
add_subdirectory(${CMAKE_SOURCE_DIR}/resource/)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/)
add_subdirectory(${CMAKE_SOURCE_DIR}/cpack/)

## build test if enabled
if(SHADOW_TEST STREQUAL ON)
    message(STATUS "SHADOW_TEST enabled")
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/test/)
endif(SHADOW_TEST STREQUAL ON)

## install our 'exported' libs so they can be imported by others
file(GLOB CMAKE_CUSTOM_MODULES "${CMAKE_SOURCE_DIR}/cmake/*cmake")
install(FILES ${CMAKE_CUSTOM_MODULES} DESTINATION share/cmake/Modules/)
if(SHADOW_EXPORT STREQUAL ON)
    install(EXPORT shadow-externals DESTINATION share/)
endif(SHADOW_EXPORT STREQUAL ON)

