project(llvm-hoist CXX)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

set(CMAKE_CXX_FLAGS "-g -fno-rtti -std=c++11 -std=gnu++11 -Wno-unknown-attributes -Wno-unused-command-line-argument")

## first try to use the LLVM package itself
find_package(LLVM)
if("${LLVM_CMAKE_DIR}" STREQUAL "")
    ## fall back to our custom find package template at cmake/FindLLVM.cmake
    find_package(LLVM QUIET CONFIG REQUIRED)
    message(STATUS "Found components for LLVM")
    message(STATUS "LLVM_PACKAGE_VERSION = ${LLVM_PACKAGE_VERSION}")
    message(STATUS "LLVM_CMAKE_DIR = ${LLVM_CMAKE_DIR}")
endif()

## Make sure we can find the cmake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_CMAKE_DIR}")
## Define add_llvm_* macro's.
set(LLVM_ON_UNIX 1)
set(LLVM_ENABLE_PLUGINS ON)
## work around llvm bugs in cmake file
## these must be set, otherwise empty args will cause function call errors
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)
set(LLVM_PLUGIN_EXT ".so")
include(AddLLVM)

#message(STATUS "Found components for LLVM")
if(LLVM_DEFINITIONS)
    message(STATUS "LLVM_DEFINITIONS = ${LLVM_DEFINITIONS}")
    add_definitions(${LLVM_DEFINITIONS})
endif()
if(LLVM_INCLUDE_DIRS)
    message(STATUS "LLVM_INCLUDE_DIRS = ${LLVM_INCLUDE_DIRS}")
    include_directories(${LLVM_INCLUDE_DIRS})
endif()
if(LLVM_LIBRARY_DIRS)
    message(STATUS "LLVM_LIBRARY_DIRS = ${LLVM_LIBRARY_DIRS}")
    link_directories(${LLVM_LIBRARY_DIRS})
endif()

## The LLVM plugin to hoist out static and global variables
## adds a 'LLVMHoistGlobals' target
add_llvm_loadable_module(LLVMHoistGlobals HoistGlobals.cpp)
