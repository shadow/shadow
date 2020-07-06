## (c) 2010-2012 Shadow, Rob Jansen jansen@cs.umn.edu

## set build parameters
project(Shadow)
set(SHADOW_VERSION_FULL 2.0.0-pre)

## This tells cmake to generate a database of compilation commands `compile_commands.json`,
## which is used by tools such as YouCompleteMe and include-what-you-use.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")

## ensure cmake version
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

## building with support for C11 on platforms that default to C99 or C89
set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")

## ensure unix environment (CMAKE_SYSTEM_NAME == "Linux")
if((NOT UNIX) OR (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux")))
    message(FATAL_ERROR "Shadow requires a Unix/Linux environment.")
endif((NOT UNIX) OR (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux")))

## ensure out-of-source build
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Shadow requires an out-of-source build. Please create a separate build directory and run 'cmake path/to/shadow [options]' there.")
endif(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

## additional user-defined include directories
foreach(include ${CMAKE_EXTRA_INCLUDES})
    include_directories(${include})
    set(CMAKE_MODULE_PATH "${include}" ${CMAKE_MODULE_PATH})
endforeach(include)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})

message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

## Mismatched C and C++ compilers can result in confusing errors at link time.
## Try to catch it here.
if(((CMAKE_C_COMPILER MATCHES "[/^]gcc") AND (NOT (CMAKE_CXX_COMPILER MATCHES "[/^]g\\+\\+")))
   OR ((CMAKE_C_COMPILER MATCHES "[/^]clang") AND (NOT (CMAKE_CXX_COMPILER MATCHES "[/^]clang\\+\\+"))))
    message(FATAL_ERROR "Mismatched C and C++ compiler ${CMAKE_C_COMPILER} and ${CMAKE_CXX_COMPILER}")
endif()

## additional user-defined library directories
foreach(library ${CMAKE_EXTRA_LIBRARIES})
    link_directories(${library})
endforeach(library)

## get general includes
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(TestBigEndian)
include(ShadowTools)

## general tests and configurations
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

## Check if we are able to compile various compilation units
compile_test(HAVE_STRUCT_STATX 
            ${CMAKE_SOURCE_DIR}/cmake/compile_tests/struct_statx.c)
compile_test(HAVE_STRUCT_STATX_TIMESTAMP 
            ${CMAKE_SOURCE_DIR}/cmake/compile_tests/struct_statx_timestamp.c)
compile_test(HAVE_STRUCT_LINUX_DIRENT 
            ${CMAKE_SOURCE_DIR}/cmake/compile_tests/struct_linux_dirent.c)
compile_test(HAVE_STRUCT_LINUX_DIRENT64 
            ${CMAKE_SOURCE_DIR}/cmake/compile_tests/struct_linux_dirent64.c)

## construct info/version strings
string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+(\\-[a-z]+)?(\\.[0-9]+)?$" "\\1" SHADOW_VERSION_MAJOR ${SHADOW_VERSION_FULL})
string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+(\\-[a-z]+)?(\\.[0-9]+)?$" "\\1" SHADOW_VERSION_MINOR ${SHADOW_VERSION_FULL})
string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)(\\-[a-z]+)?(\\.[0-9]+)?$" "\\1" SHADOW_VERSION_PATCH ${SHADOW_VERSION_FULL})

message(STATUS "SHADOW_VERSION_FULL=${SHADOW_VERSION_FULL}")
message(STATUS "SHADOW_VERSION_MAJOR=${SHADOW_VERSION_MAJOR}")
message(STATUS "SHADOW_VERSION_MINOR=${SHADOW_VERSION_MINOR}")
message(STATUS "SHADOW_VERSION_PATCH=${SHADOW_VERSION_PATCH}")

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
    set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} v${SHADOW_VERSION_MAJOR}.${SHADOW_VERSION_MINOR}.${SHADOW_VERSION_PATCH}")
endif()

message(STATUS "Building ${SHADOW_VERSION_STRING_CONF}")

## current build date
string(TIMESTAMP BUILDDATE "%Y-%m-%d")
if(BUILDDATE)
    set(SHADOW_VERSION_STRING_CONF "${SHADOW_VERSION_STRING_CONF} (built ${BUILDDATE})")
endif(BUILDDATE)

## setup shadow options
option(SHADOW_DEBUG "turn on debugging for verbose program output (default: OFF)" OFF)
option(SHADOW_PROFILE "build with profile settings (default: OFF)" OFF)
option(SHADOW_TEST "build tests (default: OFF)" OFF)
option(SHADOW_EXPORT "export service libraries and headers (default: OFF)" OFF)
option(SHADOW_WERROR "turn compiler warnings into errors. (default: OFF)" OFF)
option(SHADOW_COVERAGE "enable code-coverage instrumentation. (default: OFF)" OFF)

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
add_compile_options(-ggdb)
add_compile_options(-fno-omit-frame-pointer)
if(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Debug enabled.")
    set(CMAKE_BUILD_TYPE Debug)
    add_definitions(-DDEBUG)
    add_compile_options(-O0)
else(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "CMAKE_BUILD_TYPE Release enabled.")
    set(CMAKE_BUILD_TYPE Release)
    add_definitions(-DNDEBUG)
    add_compile_options(-O3)
endif(SHADOW_DEBUG STREQUAL ON)
if(SHADOW_WERROR STREQUAL ON)
    add_compile_options(-Werror)
endif(SHADOW_WERROR STREQUAL ON)
if(SHADOW_COVERAGE STREQUAL ON)
    add_compile_options(--coverage)
    add_ldflags(--coverage)
endif(SHADOW_COVERAGE STREQUAL ON)

if($ENV{VERBOSE})
    add_definitions(-DVERBOSE)
endif()

if(SHADOW_PROFILE STREQUAL ON)
    add_definitions(-DDEBUG)
    ## see src/main/CMakeLists.txt, where we add the -pg flags
endif(SHADOW_PROFILE STREQUAL ON)

if(SHADOW_EXPORT STREQUAL ON)
    ## the actual work happens in the CMakeLists files in each plug-in directory
    MESSAGE(STATUS "will export Shadow plug-in service libraries and headers")
endif(SHADOW_EXPORT STREQUAL ON)

## generate config header and make sure its on the include path
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/src/shd-config.h)
install(FILES ${CMAKE_BINARY_DIR}/src/shd-config.h DESTINATION include/)

#if(POLICY  CMP0026)
#    cmake_policy(SET  CMP0026  OLD)
#endif()

if(SHADOW_TEST STREQUAL ON)
    ## http://www.cmake.org/Wiki/CMake_Testing_With_CTest
    message(STATUS "SHADOW_TEST enabled")
    enable_testing()
endif(SHADOW_TEST STREQUAL ON)

## recurse our project tree
add_subdirectory(${CMAKE_SOURCE_DIR}/resource/)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/)
add_subdirectory(${CMAKE_SOURCE_DIR}/cpack/)

## install our 'exported' libs so they can be imported by others
file(GLOB CMAKE_CUSTOM_MODULES "${CMAKE_SOURCE_DIR}/cmake/*cmake")
install(FILES ${CMAKE_CUSTOM_MODULES} DESTINATION share/cmake/Modules/)
if(SHADOW_EXPORT STREQUAL ON)
    install(EXPORT shadow-externals DESTINATION share/)
endif(SHADOW_EXPORT STREQUAL ON)

