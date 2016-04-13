add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)
add_cflags(-Wno-invalid-noreturn)

## LLVM will be used to build each plug-in that was not disabled in setup.py
## our custom pass will enable us to extract and swap plugin state automatically
include(LLVMTools)
add_subdirectory(llvm-hoist-pass)

## make sure the LLVMHoistGlobals library is built correctly
## the plugins need the absolute path to load the llvm hoist pass plugin
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## build the plug-ins

if(NOT BUILD_TGEN STREQUAL OFF)
    add_subdirectory(shadow-plugin-tgen)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shadow-plugin-tor)
    add_subdirectory(shadow-plugin-tor)
endif()

if(SHADOW_TEST STREQUAL ON)
    add_subdirectory(shadow-plugin-test)
endif()