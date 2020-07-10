## TODO if we have cmake > 3.1, can we do the following?
#pkg_check_modules (GLIB2   glib-2.0>=2.32)
#message(STATUS "GLIB2_VERSION = ${GLIB2_VERSION}")

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${IGRAPH_INCLUDES} ${GLIB_INCLUDES})

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

if(SHADOW_DEBUG STREQUAL ON)
    message(STATUS "Building Rust library in debug mode.")
    set(RUST_BUILD_TYPE "debug")
    set(RUST_BUILD_FLAG "")
else()
    message(STATUS "Building Rust library in release mode.")
    set(RUST_BUILD_TYPE "release")
    set(RUST_BUILD_FLAG "--release")
endif()

include(ExternalProject)

## build the rust library
ExternalProject_Add(
    shadow-rust-project
    PREFIX ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} --all-targets --target-dir ${CMAKE_CURRENT_BINARY_DIR}/target"
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/target/debug/libshadow_rs.a ${CMAKE_CURRENT_BINARY_DIR}/target/release/libshadow_rs.a
    INSTALL_COMMAND ""
    LOG_BUILD OFF
)

## we can't predict exact executable names until this is fixed: https://github.com/rust-lang/cargo/issues/1924
add_test(NAME rust-unit-tests COMMAND bash -c "exec \"$(find target/${RUST_BUILD_TYPE}/deps/ \
                                               -type f -executable -name 'shadow_rs-*' -print | head -n 1)\" --color always")
set_property(TEST rust-unit-tests PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")

## allow shadow to link to the static rust library
add_library(shadow-rs STATIC IMPORTED)
set_target_properties(shadow-rs PROPERTIES IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/target/debug/libshadow_rs.a)
set_target_properties(shadow-rs PROPERTIES IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/target/release/libshadow_rs.a)
add_dependencies(shadow-rs shadow-rust-project)

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
    host/descriptor/descriptor_listener.c
    host/descriptor/descriptor_table.c
    host/descriptor/epoll.c
    host/descriptor/file.c
    host/descriptor/socket.c
    host/descriptor/tcp.c
    host/descriptor/tcp_cong.c
    host/descriptor/tcp_cong_reno.c
    host/descriptor/timer.c
    host/descriptor/transport.c
    host/descriptor/udp.c
    host/process.c
    host/cpu.c
    host/syscall_handler.c
    host/syscall/protected.c
    host/syscall/epoll.c
    host/syscall/fcntl.c
    host/syscall/file.c
    host/syscall/fileat.c
    host/syscall/ioctl.c
    host/syscall/mman.c
    host/syscall/socket.c
    host/syscall/time.c
    host/syscall/timerfd.c
    host/syscall/unistd.c
    host/syscall/uio.c
    host/thread.c
    host/host.c
    host/syscall_condition.c
    host/thread_preload.c
    host/thread_ptrace.c
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

add_library(shadow-remora STATIC ${REMORA_SRC})

add_library(shadow-tsc STATIC host/tsc.c)
target_link_libraries(shadow-tsc ${GLIB_LIBRARIES} logger)
add_executable(tsc_test host/tsc_test.c)
target_link_libraries(tsc_test ${GLIB_LIBRARIES} shadow-tsc)
add_test(NAME tsc_test COMMAND tsc_test)

set(SHD_SHMEM_SRC
    shmem/buddy.c
    shmem/shmem_allocator.c
    shmem/shmem_file.c
    shmem/shmem_util.c
    shmem/shmem_cleanup.c)

add_library(shadow-shmem STATIC ${SHD_SHMEM_SRC})
target_link_libraries(shadow-shmem logger)
add_executable(shd-shmem-test shmem/shmem_test.c)
target_link_libraries(shd-shmem-test shadow-shmem
    ${GLIB_LIBRARIES} ${RT_LIBRARIES} ${M_LIBRARIES} ${PROCPS_LIBRARIES})

include_directories(${CMAKE_SOURCE_DIR}/src/main/shmem)
add_test(NAME shmem COMMAND shd-shmem-test)

## specify the main shadow executable, build, link, and install
add_executable(shadow ${shadow_srcs})
add_dependencies(shadow shadow-remora)
target_link_libraries(shadow
   ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES}
   ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES} ${PROCPS_LIBRARIES}
   logger shadow-remora shadow-shim-helper shadow-shmem shadow-tsc shadow-rs)
install(TARGETS shadow DESTINATION bin)

## shadow needs to find libs after install
set_target_properties(shadow PROPERTIES
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    INSTALL_RPATH_USE_LINK_PATH TRUE
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_INSTALL_PREFIX}/lib"
)
