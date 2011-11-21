## make sure shadow.h is in the include path
include_directories(${SHADOW_SOURCE_DIR}/ ${GLIB_INCLUDES})

## build the required libraries for intercepting system and library calls
add_subdirectory(intercept)

## build the built-in plug-in libraries
add_subdirectory(plugin/libraries)

## sources for our main shadow program
set(shadow_srcs
    configuration/shd-examples.c
    configuration/shd-configuration.c
    configuration/shd-parser.c
    engine/shd-logging.c
    engine/shd-main.c
    engine/shd-engine.c
    engine/shd-worker.c
    
    topology/shd-address.c
    topology/shd-network.c
    topology/shd-link.c
    topology/shd-internetwork.c
    
    plugin/shd-plugin.c
    plugin/shd-plugin-state.c
    plugin/shd-software.c
    plugin/libraries/shd-library.c
    
    runnable/shd-runnable.c
    runnable/shd-listener.c
    runnable/event/shd-event.c
    runnable/event/shd-start-application.c
    runnable/event/shd-callback.c
    runnable/event/shd-interface-received.c
    runnable/event/shd-interface-sent.c
    runnable/event/shd-notify-plugin.c
    runnable/event/shd-packet-arrived.c
    runnable/event/shd-packet-dropped.c
    runnable/action/shd-action.c
    runnable/action/shd-connect-network.c
    runnable/action/shd-create-software.c
    runnable/action/shd-create-network.c
    runnable/action/shd-create-node.c
    runnable/action/shd-generate-cdf.c
    runnable/action/shd-load-cdf.c
    runnable/action/shd-load-plugin.c
    runnable/action/shd-kill-engine.c
        
    node/descriptor/shd-descriptor.c
    node/descriptor/shd-epoll.c
    node/descriptor/shd-transport.c
    node/descriptor/shd-pipe.c
    node/descriptor/shd-socket.c
    node/descriptor/shd-tcp.c
    node/descriptor/shd-udp.c
    node/shd-packet.c
    node/shd-cpu.c
    node/shd-network-interface.c
    node/shd-system.c
    node/shd-application.c
    node/shd-node.c
        
    utility/shd-cdf.c
    utility/shd-registry.c
    utility/rand.c
    
    main.c
)

## specify the main shadow executable, build, link, and install
add_executable(shadow-bin ${shadow_srcs})
add_dependencies(shadow-bin shadow-intercept shadow-preload)
target_link_libraries(shadow-bin shadow-intercept ${M_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-bin DESTINATION bin)

## shadow needs to find libshadow-intercept and custom libs after install
set_target_properties(shadow-bin PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE)

## install the helper script that sets LD_PRELOAD before launching shadow-bin
install(PROGRAMS ${SHADOW_SOURCE_DIR}/shadow DESTINATION bin)
