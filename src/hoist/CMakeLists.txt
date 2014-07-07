project(llvm-hoist CXX)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

set(CMAKE_CXX_FLAGS "-fno-rtti")

find_package(LLVM REQUIRED)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_DIR}")

# Define add_llvm_* macro's.
set(LLVM_ON_UNIX 1)
include(AddLLVM)

#message(STATUS "Found components for LLVM")
if(${LLVM_DEFINITIONS})
    message(STATUS "LLVM_DEFINITIONS = ${LLVM_DEFINITIONS}")
    add_definitions(${LLVM_DEFINITIONS})
endif()
if(${LLVM_INCLUDE_DIRS})
    message(STATUS "LLVM_INCLUDE_DIRS = ${LLVM_INCLUDE_DIRS}")
    include_directories(${LLVM_INCLUDE_DIRS})
endif()
if(${LLVM_LIBRARY_DIRS})
    message(STATUS "LLVM_LIBRARY_DIRS = ${LLVM_LIBRARY_DIRS}")
    link_directories(${LLVM_LIBRARY_DIRS})
endif()

## The LLVM plugin to hoist out static and global variables
## adds a 'LLVMHoistGlobals' target
add_llvm_loadable_module(LLVMHoistGlobals HoistGlobals.cpp)
