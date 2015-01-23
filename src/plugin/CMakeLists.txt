## build the required libraries for intercepting system and library calls.
## create the interpose library, to be set as LD_PRELOAD to intercept functions.
## the functions we intercept MUST be in a shared library for dlsym searching.
## we redirect them to our internal shadow versions, or to libc.
add_library(shadow-interpose SHARED shd-interposer.c)
target_link_libraries(shadow-interpose dl)
install(TARGETS shadow-interpose DESTINATION lib)

## LLVM will be used to build each plug-in that was not disabled in setup.py
## our custom pass will enable us to extract and swap plugin state automatically
include(LLVMTools)
add_subdirectory(llvm-hoist-pass)

## make sure the LLVMHoistGlobals library is built correctly
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)
message(STATUS "LLVMHoistGlobalsPATH = ${LLVMHoistGlobalsPATH}")
if((NOT DEFINED LLVMHoistGlobalsPATH) OR ( "${LLVMHoistGlobalsPATH}" STREQUAL ""))
    message(FATAL_ERROR "LLVMHoistGlobalsPATH is empty: was there an error building src/hoist/HoistGlobals.cpp?")
endif()

## the plug-ins need to include the interface header
include_directories(${CMAKE_SOURCE_DIR}/src/plugin)
install(FILES shd-library.h shadow-plugin-interface.h DESTINATION include/)

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_definitions(-D_GNU_SOURCE)
add_cflags("-fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

if(NOT BUILD_TGEN STREQUAL OFF)
    add_subdirectory(shadow-plugin-tgen)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shadow-plugin-tor)
    add_subdirectory(shadow-plugin-tor)
endif()