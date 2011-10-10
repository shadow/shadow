#include_directories(${SHADOW_SOURCE_DIR}/core)
#include_directories(${SHADOW_SOURCE_DIR}/core/vnetwork)
#include_directories(${SHADOW_SOURCE_DIR}/core/vsystem)
#include_directories(${SHADOW_SOURCE_DIR}/core/node)
#include_directories(${SHADOW_SOURCE_DIR}/core/vevent)
#include_directories(${SHADOW_SOURCE_DIR}/dsim)
#include_directories(${SHADOW_SOURCE_DIR}/intercept)
#include_directories(${SHADOW_SOURCE_DIR}/util)
#include_directories(${SHADOW_SOURCE_DIR}/plug-ins)

## continue recursing
#add_subdirectory(core)
#add_subdirectory(dsim)
#add_subdirectory(intercept)
#add_subdirectory(plug-ins)
#add_subdirectory(util)

## install shadow script
#install(PROGRAMS ${SHADOW_SOURCE_DIR}/shadow DESTINATION bin)

include_directories(${SHADOW_SOURCE_DIR}/)

add_subdirectory(plugin/libraries)

set(shadow2_srcs
    configuration/shd-configuration.c
    configuration/shd-parser.c
    engine/shd-logging.c
    engine/shd-main.c
    engine/shd-engine.c
    engine/shd-worker.c
    
    plugin/shd-plugin.c
    plugin/shd-plugin-state.c
    plugin/shd-shadowlib.c
    
    runnable/shd-runnable.c
    runnable/event/shd-event.c
    runnable/event/shd-start-application.c
    runnable/event/shd-spine.c
    runnable/action/shd-action.c
    runnable/action/shd-spina.c
    runnable/action/shd-connect-network.c
    runnable/action/shd-create-application.c
    runnable/action/shd-create-network.c
    runnable/action/shd-create-node.c
    runnable/action/shd-generate-cdf.c
    runnable/action/shd-load-cdf.c
    runnable/action/shd-load-plugin.c
    runnable/action/shd-kill-engine.c
        
    node/shd-node.c
    node/shd-application.c
    
    utility/shd-cdf.c
    utility/shd-data-entry.c
    utility/shd-registry.c
    utility/shd-resolver.c
    utility/shd-topology.c
    
    main.c
)

add_executable(shadow2 ${shadow2_srcs})
target_link_libraries(shadow2 ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow2 DESTINATION bin)