## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

## TODO if we have cmake > 3.1, can we do the following?
#pkg_check_modules (GLIB2   glib-2.0>=2.32)
#message(STATUS "GLIB2_VERSION = ${GLIB2_VERSION}")

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

## make sure shadow.h is in the include path
include_directories(${CMAKE_SOURCE_DIR}/src/)

## build plug-in libraries and other components
add_subdirectory(plugin)

## build rpth and make sure rpth.h is in the include path
add_subdirectory(external)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/external/rpth)
link_directories(${CMAKE_CURRENT_BINARY_DIR}/external/rpth/.libs)

## compile defs and flags
#add_definitions(-D_SVID_SOURCE -D_XOPEN_SOURCE=600 -D_ISOC11_SOURCE) #-D_GNU_SOURCE
## set the igraph version guesses
add_definitions(-DIGRAPH_VERSION_MAJOR_GUESS=${IGRAPH_VERSION_MAJOR_GUESS})
add_definitions(-DIGRAPH_VERSION_MINOR_GUESS=${IGRAPH_VERSION_MINOR_GUESS})
add_definitions(-DIGRAPH_VERSION_PATCH_GUESS=${IGRAPH_VERSION_PATCH_GUESS})
add_cflags(-fPIC)
add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)
add_cflags(-Wno-visibility)

## sources for our main shadow program
set(shadow_srcs
    engine/shd-main.c
    engine/shd-master.c
    engine/shd-slave.c
    engine/shd-worker.c

    host/descriptor/shd-channel.c
    host/descriptor/shd-descriptor.c
    host/descriptor/shd-epoll.c
    host/descriptor/shd-socket.c
    host/descriptor/shd-tcp.c
    host/descriptor/shd-tcp-aimd.c
    host/descriptor/shd-tcp-congestion.c
    host/descriptor/shd-tcp-cubic.c
    host/descriptor/shd-tcp-reno.c
    host/descriptor/shd-tcp-scoreboard.c
    host/descriptor/shd-timer.c
    host/descriptor/shd-transport.c
    host/descriptor/shd-udp.c
    host/shd-process.c
    host/shd-program.c
    host/shd-cpu.c
    host/shd-host.c
    host/shd-network-interface.c
    host/shd-packet.c
    host/shd-tracker.c

    runnable/action/shd-action.c
    runnable/action/shd-create-node.c
    runnable/action/shd-kill-engine.c
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
    runnable/event/shd-tcp-retransmit-timer-expired.c
    runnable/shd-listener.c
    runnable/shd-runnable.c

    support/shd-configuration.c
    support/shd-examples.c
    support/shd-event-queue.c
    support/shd-logging.c
    support/shd-parser.c
    support/shd-pcap-writer.c

    topology/shd-address.c
    topology/shd-dns.c
    topology/shd-path.c
    topology/shd-topology.c

    utility/shd-async-priority-queue.c
    utility/shd-byte-queue.c
    utility/shd-count-down-latch.c
    utility/shd-priority-queue.c
    utility/shd-random.c
    utility/shd-utility.c

    main.c
)

## specify the main shadow executable, build, link, and install
add_executable(shadow ${shadow_srcs})
add_dependencies(shadow shadow-interpose rpth)
target_link_libraries(shadow shadow-interpose -lrpth ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow DESTINATION bin)

## shadow needs to find libshadow-intercept and custom libs after install
set_target_properties(shadow PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE LINK_FLAGS "-Wl,--no-as-needed")
