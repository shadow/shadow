## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

## pthreads
set(CMAKE_THREAD_PREFER_PTHREAD 1)
find_package(Threads REQUIRED)
message(STATUS "Using Threads library: ${CMAKE_THREAD_LIBS_INIT}")

## TODO if we have cmake > 3.1, can we do the following?
#pkg_check_modules (GLIB2   glib-2.0>=2.32)
#message(STATUS "GLIB2_VERSION = ${GLIB2_VERSION}")

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

## make sure shadow.h is in the include path
include_directories(${PROJECT_SOURCE_DIR}/src/main)

## make sure rpth.h is in the include and link path
include_directories(${PROJECT_BINARY_DIR}/src/external/rpth)
link_directories(${PROJECT_BINARY_DIR}/src/external/rpth/.libs)

## make sure vdl.h is in the include and link path
include_directories(${PROJECT_SOURCE_DIR}/src/external/elf-loader)
include_directories(${PROJECT_BINARY_DIR}/src/external/elf-loader)
link_directories(${PROJECT_BINARY_DIR}/src/external/elf-loader)

## compile defs and flags
#add_definitions(-D_SVID_SOURCE -D_XOPEN_SOURCE=600 -D_ISOC11_SOURCE) #-D_GNU_SOURCE
## set the igraph version guesses
add_definitions(-DIGRAPH_VERSION_MAJOR_GUESS=${IGRAPH_VERSION_MAJOR_GUESS})
add_definitions(-DIGRAPH_VERSION_MINOR_GUESS=${IGRAPH_VERSION_MINOR_GUESS})
add_definitions(-DIGRAPH_VERSION_PATCH_GUESS=${IGRAPH_VERSION_PATCH_GUESS})
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
    core/logger/shd-logger.c
    core/logger/shd-logger-helper.c
    core/logger/shd-log-level.c
    core/logger/shd-log-record.c
    core/scheduler/shd-scheduler.c
    core/scheduler/shd-scheduler-policy-global-single.c
    core/scheduler/shd-scheduler-policy-host-single.c
    core/scheduler/shd-scheduler-policy-host-steal.c
    core/scheduler/shd-scheduler-policy-thread-perhost.c
    core/scheduler/shd-scheduler-policy-thread-perthread.c
    core/scheduler/shd-scheduler-policy-thread-single.c
    core/support/shd-options.c
    core/support/shd-examples.c
    core/support/shd-configuration.c
    core/support/shd-object-counter.c
    core/work/shd-event.c
    core/work/shd-message.c
    core/work/shd-task.c
    core/work/shd-command.c
    core/shd-main.c
    core/shd-master.c
    core/shd-slave.c
    core/shd-worker.c

    host/descriptor/shd-channel.c
    host/descriptor/shd-descriptor.c
    host/descriptor/shd-epoll.c
    host/descriptor/shd-socket.c
    host/descriptor/shd-tcp.c
    host/descriptor/shd-tcp-cong.c
    host/descriptor/shd-tcp-cong-reno.c
    host/descriptor/shd-timer.c
    host/descriptor/shd-eventfd.c
    host/descriptor/shd-transport.c
    host/descriptor/shd-udp.c
    host/shd-process.c
    host/shd-cpu.c
    host/shd-host.c
    host/shd-network-interface.c
    host/shd-tracker.c
    host/bleep_related/shd-bleep.c

    routing/shd-payload.c
    routing/shd-packet.c
    routing/shd-address.c
    routing/shd-dns.c
    routing/shd-path.c
    routing/shd-topology.c

    utility/shd-async-priority-queue.c
    utility/shd-byte-queue.c
    utility/shd-count-down-latch.c
    utility/shd-pcap-writer.c
    utility/shd-priority-queue.c
    utility/shd-random.c
    utility/shd-utility.c

    main.c
)

set(REMORA_SRC
   host/descriptor/shd-tcp-retransmit-tally.cc
)

add_library(shadow-remora SHARED ${REMORA_SRC})
install(TARGETS shadow-remora DESTINATION lib)

## specify the main shadow executable, build, link, and install
add_executable(shadow ${shadow_srcs})
add_dependencies(shadow shadow-remora shadow-interpose-helper elf-loader rpth)
## 'shadow-interpose-helper' and 'vdl' are cmake targets, the rest are external libs for which '-l' is needed
target_link_libraries(shadow shadow-interpose-helper vdl -lrpth
   ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES}
   ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES} shadow-remora)
install(TARGETS shadow DESTINATION bin)


## shadow needs to find libshadow-interpose and custom libs after install
set_target_properties(shadow PROPERTIES
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib,-dynamic-linker=${CMAKE_INSTALL_PREFIX}/lib/ldso -z lazy"
)
