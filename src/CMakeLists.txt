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
    
    topology/shd-network.c
    topology/shd-link.c
    topology/shd-internetwork.c
    
    plugin/shd-plugin.c
    plugin/shd-plugin-state.c
    plugin/shd-software.c
    plugin/shd-shadowlib.c
    
    runnable/shd-runnable.c
    runnable/event/shd-event.c
    runnable/event/shd-start-application.c
    runnable/event/shd-callback.c
    runnable/event/shd-packet-arrived.c
    runnable/event/shd-packet-received.c
    runnable/event/shd-packet-sent.c
    runnable/event/shd-socket-activated.c
    runnable/event/shd-socket-poll-timer-expired.c
    runnable/event/shd-tcp-close-timer-expired.c
    runnable/event/shd-tcp-dack-timer-expired.c
    runnable/event/shd-tcp-retransmit-timer-expired.c
    runnable/event/shd-spine.c
    runnable/action/shd-spina.c
    runnable/action/shd-action.c
    runnable/action/shd-connect-network.c
    runnable/action/shd-create-software.c
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
    
    utility/linkedbuffer.c
    utility/orderedlist.c
    utility/reference_counter.c
    utility/rand.c
    virtual/vbuffer.c
    virtual/vcpu.c
    virtual/vepoll.c
    virtual/vevent_mgr.c
    virtual/vevent.c
    virtual/vpacket_mgr.c
    virtual/vpacket.c
    virtual/vpeer.c
    virtual/vpipe.c
    virtual/vsocket_mgr.c
    virtual/vsocket.c
    virtual/vsystem.c
    virtual/vtcp_server.c
    virtual/vtcp.c
    virtual/vtransport_mgr.c
    virtual/vtransport.c
    virtual/vudp.c
    
    main.c
)

## specify the executable, link, and install location under prefix
add_executable(shadow ${shadow_srcs})
target_link_libraries(shadow ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow DESTINATION bin)