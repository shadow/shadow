include_directories(${SHADOW_SOURCE_DIR}/core)
include_directories(${SHADOW_SOURCE_DIR}/core/vnetwork)
include_directories(${SHADOW_SOURCE_DIR}/core/vsystem)
include_directories(${SHADOW_SOURCE_DIR}/core/node)
include_directories(${SHADOW_SOURCE_DIR}/core/vevent)
include_directories(${SHADOW_SOURCE_DIR}/dsim)
include_directories(${SHADOW_SOURCE_DIR}/intercept)
include_directories(${SHADOW_SOURCE_DIR}/util)
include_directories(${SHADOW_SOURCE_DIR}/plug-ins)

## continue recursing
add_subdirectory(core)
add_subdirectory(dsim)
add_subdirectory(intercept)
add_subdirectory(plug-ins)
add_subdirectory(util)

## install shadow script
install(PROGRAMS ${SHADOW_SOURCE_DIR}/shadow DESTINATION bin)
