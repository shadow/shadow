add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)

## the plug-ins need to include the interface header
include_directories(${CMAKE_SOURCE_DIR}/src/plugin)
install(FILES shadow-plugin-interface.h DESTINATION include/)

## LLVM will be used to build each plug-in that was not disabled in setup.py
## our custom pass will enable us to extract and swap plugin state automatically
include(LLVMTools)
add_subdirectory(llvm-hoist-pass)

## make sure the LLVMHoistGlobals library is built correctly
## the plugins need the absolute path to load the llvm hoist pass plugin
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## build the plug-ins
add_subdirectory(shadow-plugin-test)

if(NOT BUILD_TGEN STREQUAL OFF)
    add_subdirectory(shadow-plugin-tgen)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shadow-plugin-tor)
    add_subdirectory(shadow-plugin-tor)
endif()