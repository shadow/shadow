project(llvm-hoist CXX)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

set(CMAKE_CXX_FLAGS "-fno-rtti")
find_package(LLVM REQUIRED)

# Define add_llvm_* macro's.
include(AddLLVM)

message(STATUS "Found components for LLVM")
message(STATUS "LLVM_DEFINITIONS = ${LLVM_DEFINITIONS}")
message(STATUS "LLVM_INCLUDE_DIRS = ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_LIBRARY_DIRS = ${LLVM_LIBRARY_DIRS}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

## The LLVM plugin to hoist out static and global variables
## adds a 'LLVMHoistGlobals' target
add_llvm_loadable_module(LLVMHoistGlobals HoistGlobals.cpp)
