add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)
add_cflags(-Wno-invalid-noreturn)

## build the plug-ins

if(NOT BUILD_TGEN STREQUAL OFF)
    add_subdirectory(shadow-plugin-tgen)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shadow-plugin-tor)
    add_subdirectory(shadow-plugin-tor)
endif()
