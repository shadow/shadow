## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## make sure shadow.h is in the include path
include_directories(${CMAKE_SOURCE_DIR}/src/)

## compile flags
add_definitions(-Wall --std=c99 -D_SVID_SOURCE -D_XOPEN_SOURCE=600 -fPIC) #-D_GNU_SOURCE

## build the required libraries for intercepting system and library calls
add_subdirectory(intercept)

## build the built-in plug-in libraries
add_subdirectory(library)

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
    plugin/shd-system.c
    library/shd-library.c
    
    runnable/shd-runnable.c
    runnable/shd-listener.c
    runnable/event/shd-event.c
    runnable/event/shd-start-application.c
    runnable/event/shd-heartbeat.c
    runnable/event/shd-callback.c
    runnable/event/shd-interface-received.c
    runnable/event/shd-interface-sent.c
    runnable/event/shd-notify-plugin.c
    runnable/event/shd-packet-arrived.c
    runnable/event/shd-packet-dropped.c
    runnable/event/shd-tcp-close-timer-expired.c
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
    node/descriptor/shd-channel.c
    node/descriptor/shd-socket.c
    node/descriptor/shd-tcp.c
    node/descriptor/shd-udp.c
    node/shd-packet.c
    node/shd-cpu.c
    node/shd-network-interface.c
    node/shd-application.c
    node/shd-tracker.c
    node/shd-node.c
        
    utility/shd-utility.c
    utility/shd-cdf.c
    utility/shd-registry.c
    utility/shd-byte-queue.c
    utility/shd-priority-queue.c
    utility/shd-async-priority-queue.c
    utility/shd-random.c
    
    main.c
)

## specify the main shadow executable, build, link, and install
add_executable(shadow-bin ${shadow_srcs})
add_dependencies(shadow-bin shadow-intercept shadow-preload)
target_link_libraries(shadow-bin shadow-intercept ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-bin DESTINATION bin)

## shadow needs to find libshadow-intercept and custom libs after install
set_target_properties(shadow-bin PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE LINK_FLAGS "-Wl,--no-as-needed")

## install the helper script that sets LD_PRELOAD before launching shadow-bin
install(PROGRAMS ${CMAKE_SOURCE_DIR}/src/shadow DESTINATION bin)
