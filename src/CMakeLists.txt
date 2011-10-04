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

set(shadow2_srcs
    engine/shd-configuration.c
    engine/shd-logging.c
    engine/shd-main.c
    engine/shd-engine.c
    engine/shd-worker.c
    events/shd-event.c
    events/shd-nodeevent.c
    events/shd-spin.c
    events/shd-killengine.c
    node/shd-node.c
    utility/shd-registry.c
    main.c
)

include_directories(${SHADOW_SOURCE_DIR}/)
add_executable(shadow2 ${shadow2_srcs})
target_link_libraries(shadow2 ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow2 DESTINATION bin)