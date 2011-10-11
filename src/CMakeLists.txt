## install shadow script
#install(PROGRAMS ${SHADOW_SOURCE_DIR}/shadow DESTINATION bin)

## make sure shadow.h is in the include path
include_directories(${SHADOW_SOURCE_DIR}/)

## build the built-in plug-in libraries
add_subdirectory(plugin/libraries)

## sources for our main shadow program
set(shadow_srcs
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
    node/shd-network.c
        
    utility/shd-cdf.c
    utility/shd-data-entry.c
    utility/shd-registry.c
    utility/shd-resolver.c
    utility/shd-topology.c
    
    main.c
)

## specify the executable, link, and install location under prefix
add_executable(shadow ${shadow_srcs})
target_link_libraries(shadow ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow DESTINATION bin)