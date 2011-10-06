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
    
    runnable/shd-runnable.c
    runnable/event/shd-event.c
    runnable/event/shd-spine.c
    runnable/action/shd-action.c
    runnable/action/shd-spina.c
    runnable/action/shd-connect-network.c
    runnable/action/shd-create-hostname.c
    runnable/action/shd-create-network.c
    runnable/action/shd-create-node.c
    runnable/action/shd-generate-cdf.c
    runnable/action/shd-load-cdf.c
    runnable/action/shd-load-plugin.c
    
    node/shd-node.c
    
    utility/shd-registry.c
    
    util/utility.c
    util/btree.c
    util/heap.c
    core/evtracker.c
    
    main.c
)

include_directories(${SHADOW_SOURCE_DIR}/)
add_executable(shadow2 ${shadow2_srcs})
target_link_libraries(shadow2 shadow-dsim ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow2 DESTINATION bin)