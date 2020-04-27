## TODO if we have cmake > 3.1, can we do the following?
#pkg_check_modules (GLIB2   glib-2.0>=2.32)
#message(STATUS "GLIB2_VERSION = ${GLIB2_VERSION}")

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

## make sure rpth.h is in the link path
link_directories(${CMAKE_BINARY_DIR}/src/external/rpth/.libs)

## make sure vdl.h is in the link path
link_directories(${CMAKE_BINARY_DIR}/src/external/elf-loader)

## compile defs and flags
#add_definitions(-D_SVID_SOURCE -D_XOPEN_SOURCE=600 -D_ISOC11_SOURCE) #-D_GNU_SOURCE
## set the igraph version guesses
add_definitions(-DIGRAPH_VERSION_MAJOR_GUESS=${IGRAPH_VERSION_MAJOR_GUESS})
add_definitions(-DIGRAPH_VERSION_MINOR_GUESS=${IGRAPH_VERSION_MINOR_GUESS})
add_definitions(-DIGRAPH_VERSION_PATCH_GUESS=${IGRAPH_VERSION_PATCH_GUESS})
add_definitions(-D_GNU_SOURCE)
add_cflags(-fPIC)
#add_cflags(-Wno-unknown-attributes)
#add_cflags(-Wno-unused-command-line-argument)
#add_cflags(-Wno-unknown-warning-option)
#add_cflags(-Wno-visibility)
add_cflags(-std=gnu99)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXXFLAGS} -std=c++11 -fPIC")

if(SHADOW_PROFILE STREQUAL ON)
    message(STATUS "Building Shadow core with profiling support using '-pg'")
    add_cflags(-pg)
endif(SHADOW_PROFILE STREQUAL ON)

## sources for our main shadow program
set(shadow_srcs
    core/logger/logger_helper.c
    core/logger/log_record.c
    core/logger/shadow_logger.c
    core/scheduler/scheduler.c
    core/scheduler/scheduler_policy_global_single.c
    core/scheduler/scheduler_policy_host_single.c
    core/scheduler/scheduler_policy_host_steal.c
    core/scheduler/scheduler_policy_thread_perhost.c
    core/scheduler/scheduler_policy_thread_perthread.c
    core/scheduler/scheduler_policy_thread_single.c
    core/support/options.c
    core/support/examples.c
    core/support/configuration.c
    core/support/object_counter.c
    core/work/event.c
    core/work/message.c
    core/work/task.c
    core/main.c
    core/master.c
    core/slave.c
    core/worker.c

    host/descriptor/channel.c
    host/descriptor/descriptor.c
    host/descriptor/epoll.c
    host/descriptor/socket.c
    host/descriptor/tcp.c
    host/descriptor/tcp_cong.c
    host/descriptor/tcp_cong_reno.c
    host/descriptor/timer.c
    host/descriptor/transport.c
    host/descriptor/udp.c
    host/process.c
    host/cpu.c
    host/host.c
    host/network_interface.c
    host/tracker.c

    routing/payload.c
    routing/packet.c
    routing/address.c
    routing/router_queue_single.c
    routing/router_queue_static.c
    routing/router_queue_codel.c
    routing/router.c
    routing/dns.c
    routing/path.c
    routing/topology.c

    utility/async_priority_queue.c
    utility/byte_queue.c
    utility/count_down_latch.c
    utility/pcap_writer.c
    utility/priority_queue.c
    utility/random.c
    utility/utility.c

    main.c
)

set(REMORA_SRC
   host/descriptor/tcp_retransmit_tally.cc
)

add_library(shadow-remora SHARED ${REMORA_SRC})
install(TARGETS shadow-remora DESTINATION lib)

## specify the main shadow executable, build, link, and install
add_executable(shadow ${shadow_srcs})
add_dependencies(shadow elf-loader rpth)
## 'shadow-interpose-helper' and 'vdl' are cmake targets, the rest are external libs for which '-l' is needed
target_link_libraries(shadow shadow-interpose-helper vdl -lrpth
   ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES}
   ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES} shadow-remora logger)
install(TARGETS shadow DESTINATION bin)


## shadow needs to find libshadow-interpose and custom libs after install
set_target_properties(shadow PROPERTIES
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib,-dynamic-linker=${CMAKE_INSTALL_PREFIX}/lib/ldso -z lazy"
)
