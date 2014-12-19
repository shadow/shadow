## the plug-ins need to include shd-library.h
include_directories(${CMAKE_SOURCE_DIR}/src/plugin)
install(FILES shd-library.h DESTINATION include/)

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_definitions(-D_GNU_SOURCE)
add_cflags("-fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## now use LLVM to build each plug-in that was not disabled in setup.py
include(LLVMTools)
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)
message(STATUS "LLVMHoistGlobalsPATH = ${LLVMHoistGlobalsPATH}")

if(NOT BUILD_TGEN STREQUAL OFF)
    add_subdirectory(tgen)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shadow-plugin-tor)
    add_subdirectory(shadow-plugin-tor)
endif()