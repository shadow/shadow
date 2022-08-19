include(CheckSymbolExists)

list(APPEND CMAKE_REQUIRED_INCLUDES ${GLIB_INCLUDE_DIRS})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${GLIB_LIBRARIES})

check_symbol_exists(g_memdup2 "glib.h" HAS_MEMDUP2)

if(HAS_MEMDUP2)
    add_compile_definitions(HAS_MEMDUP2)
endif()

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDE_DIRS})

## link to preload libs (rpath will not contain these paths when shadow is installed)
link_directories(${CMAKE_BINARY_DIR}/src/lib/shim)
link_directories(${CMAKE_BINARY_DIR}/src/lib/shmem)
link_directories(${CMAKE_BINARY_DIR}/src/lib/injector_preload)
link_directories(${CMAKE_BINARY_DIR}/src/lib/libc_preload)
link_directories(${CMAKE_BINARY_DIR}/src/lib/openssl_preload)

## compile defs and flags
#add_definitions(-D_SVID_SOURCE -D_XOPEN_SOURCE=600 -D_ISOC11_SOURCE) #-D_GNU_SOURCE
add_definitions(-D_GNU_SOURCE)
add_cflags(-fPIC)
#add_cflags(-Wno-unknown-attributes)
#add_cflags(-Wno-unused-command-line-argument)
#add_cflags(-Wno-unknown-warning-option)
#add_cflags(-Wno-visibility)

# Enable C11.
add_cflags(-std=gnu11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXXFLAGS} -std=c++11 -fPIC")

if(SHADOW_PROFILE STREQUAL ON)
    message(STATUS "Building Shadow core with profiling support using '-pg'")
    add_cflags(-pg)
endif(SHADOW_PROFILE STREQUAL ON)

add_library(shadow-remora STATIC host/descriptor/tcp_retransmit_tally.cc)
target_link_libraries(shadow-remora INTERFACE "-lstdc++")

## sources for our main shadow program
set(shadow_srcs
    core/logger/log_wrapper.c
    core/scheduler/scheduler.c
    core/scheduler/scheduler_policy.c
    core/support/config_handlers.c
    core/main.c
    core/worker.c

    host/descriptor/descriptor.c
    host/status_listener.c
    host/descriptor/compat_socket.c
    host/descriptor/epoll.c
    host/descriptor/regular_file.c
    host/descriptor/socket.c
    host/descriptor/tcp.c
    host/descriptor/tcp_cong.c
    host/descriptor/tcp_cong_reno.c
    host/descriptor/timerfd.c
    host/descriptor/transport.c
    host/descriptor/udp.c
    host/affinity.c
    host/process.c
    host/cpu.c
    host/futex.c
    host/futex_table.c
    host/shimipc.c
    host/syscall_handler.c
    host/syscall_types.c
    host/syscall/protected.c
    host/syscall/clone.c
    host/syscall/epoll.c
    host/syscall/fcntl.c
    host/syscall/file.c
    host/syscall/fileat.c
    host/syscall/futex.c
    host/syscall/ioctl.c
    host/syscall/mman.c
    host/syscall/poll.c
    host/syscall/process.c
    host/syscall/select.c
    host/syscall/shadow.c
    host/syscall/signal.c
    host/syscall/socket.c
    host/syscall/time.c
    host/syscall/timerfd.c
    host/syscall/unistd.c
    host/syscall/uio.c
    host/thread.c
    host/host.c
    host/syscall_condition.c
    host/managed_thread.c
    host/network_interface.c
    host/network_queuing_disciplines.c
    host/tracker.c

    routing/payload.c
    routing/packet.c
    routing/address.c
    routing/router_queue_single.c
    routing/router_queue_static.c
    routing/router_queue_codel.c
    routing/router.c
    routing/dns.c

    utility/async_priority_queue.c
    utility/count_down_latch.c
    utility/priority_queue.c
    utility/rpath.c
    utility/tagged_ptr.c
    utility/utility.c
)
add_library(shadow-c STATIC ${shadow_srcs})
target_link_libraries(shadow-c
   INTERFACE
   ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES}
   PkgConfig::GLIB
   shadow-shim-helper logger shadow-remora shadow-shmem shadow-tsc)

set(CARGO_ENV_VARS "${CARGO_ENV_VARS} SHADOW_VERSION=\"${SHADOW_VERSION_STRING_CONF}\"")
set(RUST_FEATURES "")
if(SHADOW_USE_PERF_TIMERS STREQUAL ON)
  set(RUST_FEATURES "${RUST_FEATURES} perf_timers")
endif()
rust_static_lib(shadow-rs FEATURES "${RUST_FEATURES}" CARGO_ENV_VARS "${CARGO_ENV_VARS}" INTERFACE_LIBRARIES shadow-c)

## Allow circular dependencies between shadow-rs and shadow-c. Without
#--start-group and --end-group, # the linker will drop object files in the first
#listed library that aren't referenced from an # earlier dependency.
## See https://gitlab.kitware.com/cmake/cmake/-/issues/21511#note_865669
add_library(shadow-c-and-rs INTERFACE)
target_link_libraries(shadow-c-and-rs INTERFACE "-Wl,--start-group" shadow-rs shadow-c "-Wl,--end-group")

## specify the main shadow executable, build, link, and install
add_executable(shadow main.c)
target_link_libraries(shadow shadow-c-and-rs)
install(TARGETS shadow DESTINATION bin)

## shadow needs to find libs after install
set_target_properties(shadow PROPERTIES LINK_FLAGS "-Wl,--no-as-needed")

add_subdirectory(bindings)
