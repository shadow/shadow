## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

## make sure shadow.h is in the include path
include_directories(${CMAKE_SOURCE_DIR}/src/)

## build the LLVM plugin that we'll use to get plugin state automatically
add_subdirectory(hoist)

## compile defs and flags
add_definitions(-D_SVID_SOURCE -D_XOPEN_SOURCE=600 -D_ISOC11_SOURCE) #-D_GNU_SOURCE
add_cflags(-fPIC)

## build the required libraries for intercepting system and library calls
add_subdirectory(interposition)

## build the built-in plug-in libraries
add_subdirectory(plugins)

## sources for our main shadow program
set(shadow_srcs
    support/shd-configuration.c
    support/shd-examples.c
    support/shd-parser.c

    engine/shd-engine.c
    support/shd-event-queue.c
    support/shd-logging.c
    engine/shd-main.c
    support/shd-plugin.c
    support/shd-system.c
    engine/shd-worker.c
    
    host/descriptor/shd-channel.c
    host/descriptor/shd-descriptor.c
    host/descriptor/shd-epoll.c
    host/descriptor/shd-socket.c
    host/descriptor/shd-tcp.c
    host/descriptor/shd-transport.c
    host/descriptor/shd-udp.c
    host/shd-application.c
    host/shd-cpu.c
    host/shd-host.c
    host/shd-network-interface.c
    host/shd-packet.c
    host/shd-tracker.c
    
    plugins/shd-library.c
    
    runnable/action/shd-action.c
    runnable/action/shd-create-node.c
    runnable/action/shd-generate-cdf.c
    runnable/action/shd-kill-engine.c
    runnable/action/shd-load-cdf.c
    runnable/action/shd-load-plugin.c
    runnable/action/shd-load-topology.c
    runnable/event/shd-callback.c
    runnable/event/shd-event.c
    runnable/event/shd-heartbeat.c
    runnable/event/shd-interface-received.c
    runnable/event/shd-interface-sent.c
    runnable/event/shd-notify-plugin.c
    runnable/event/shd-packet-arrived.c
    runnable/event/shd-packet-dropped.c
    runnable/event/shd-start-application.c
    runnable/event/shd-stop-application.c
    runnable/event/shd-tcp-close-timer-expired.c
    runnable/shd-listener.c
    runnable/shd-runnable.c

    topology/shd-address.c
    topology/shd-cluster.c
    topology/shd-dns.c
    topology/shd-path.c
    topology/shd-topology.c
        
    utility/shd-async-priority-queue.c
    utility/shd-byte-queue.c
    utility/shd-cdf.c
    utility/shd-count-down-latch.c
    utility/shd-priority-queue.c
    utility/shd-random.c
    utility/shd-registry.c
    utility/shd-utility.c
    
    main.c
)

## specify the main shadow executable, build, link, and install
add_executable(shadow-bin ${shadow_srcs})
add_dependencies(shadow-bin shadow-intercept shadow-preload)
target_link_libraries(shadow-bin shadow-intercept ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-bin DESTINATION bin)

## shadow needs to find libshadow-intercept and custom libs after install
set_target_properties(shadow-bin PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE LINK_FLAGS "-Wl,--no-as-needed")

## install the helper script that sets LD_PRELOAD before launching shadow-bin
install(PROGRAMS ${CMAKE_SOURCE_DIR}/src/shadow DESTINATION bin)
