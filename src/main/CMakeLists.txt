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

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    message(STATUS "Building Rust library in debug mode.")
    set(RUST_BUILD_TYPE "debug")
    set(RUST_BUILD_FLAG "")
else()
    message(STATUS "Building Rust library in release mode.")
    set(RUST_BUILD_TYPE "release")
    set(RUST_BUILD_FLAG "--release")
endif()

if(SHADOW_COVERAGE STREQUAL ON)
    # https://github.com/shadow/shadow/issues/1236
    set(RUSTFLAGS "${RUSTFLAGS} --remap-path-prefix \"=${CMAKE_CURRENT_SOURCE_DIR}/\"")
endif()

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
target_link_libraries(shadow-c INTERFACE
   ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES}
   PkgConfig::GLIB
   shadow-shim-helper logger shadow-remora shadow-shmem shadow-tsc)

# All targets that our Rust project directly depends on.
set(LIBS_TO_PROCESS "shadow-c")

# Process each dependency, adding the compiler flags for individual libraries,
# and recursively processing their dependencies.
set(LIBS_SEEN "${LIBS_TO_PROCESS}")
while(NOT "${LIBS_TO_PROCESS}" STREQUAL "")
    # The list POP_FRONT operation would be a little nicer here, but it needs
    # cmake 3.15.
    list(GET LIBS_TO_PROCESS 0 INTERFACE_LINK_LIBRARY)
    list(REMOVE_AT LIBS_TO_PROCESS 0)
    if(INTERFACE_LINK_LIBRARY MATCHES "(.*)/lib(.*)\(.so|.a)")
        set(RUSTFLAGS "${RUSTFLAGS} -L${CMAKE_MATCH_1} -l${CMAKE_MATCH_2}")
    elseif(INTERFACE_LINK_LIBRARY MATCHES "^-l.*")
        set(RUSTFLAGS "${RUSTFLAGS} ${INTERFACE_LINK_LIBRARY}")
    else()
        get_target_property(TYPE ${INTERFACE_LINK_LIBRARY} TYPE)

        get_target_property(IMPORTED ${INTERFACE_LINK_LIBRARY} IMPORTED)
        if ("${IMPORTED}" STREQUAL "FALSE")
            list(APPEND RUST_DEPENDS "${INTERFACE_LINK_LIBRARY}")
        endif()

        if ("${TYPE}" STREQUAL "STATIC_LIBRARY")
            get_target_property(BINARY_DIR ${INTERFACE_LINK_LIBRARY} BINARY_DIR)
            set(RUSTFLAGS "${RUSTFLAGS} -L${BINARY_DIR}")

            get_target_property(NAME ${INTERFACE_LINK_LIBRARY} NAME)
            set(RUSTFLAGS "${RUSTFLAGS} -l${NAME}")
        elseif("${TYPE}" STREQUAL "INTERFACE_LIBRARY")
            # Nothing to do for the target itself, but we'll recurse
            # into its dependencies below.
        else()
            message(FATAL_ERROR "Unhandled TYPE:${TYPE} for ${INTERFACE_LINK_LIBRARY}")
        endif()
        get_target_property(REC_INTERFACE_LINK_LIBRARIES ${INTERFACE_LINK_LIBRARY} INTERFACE_LINK_LIBRARIES)
        if (NOT "${REC_INTERFACE_LINK_LIBRARIES}" STREQUAL "REC_INTERFACE_LINK_LIBRARIES-NOTFOUND")
            foreach(REC_INTERFACE_LINK_LIBRARY IN LISTS REC_INTERFACE_LINK_LIBRARIES)
                list(FIND LIBS_SEEN "${REC_INTERFACE_LINK_LIBRARY}" IDX)
                if ("${IDX}" EQUAL "-1")
                    list(APPEND LIBS_TO_PROCESS "${REC_INTERFACE_LINK_LIBRARY}")
                    list(APPEND LIBS_SEEN "${REC_INTERFACE_LINK_LIBRARY}")
                endif()
            endforeach()
        endif()
    endif()
endwhile()
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} SHADOW_VERSION=\"${SHADOW_VERSION_STRING_CONF}\"")

set(RUST_FEATURES "")
if(SHADOW_USE_PERF_TIMERS STREQUAL ON)
  set(RUST_FEATURES "${RUST_FEATURES} perf_timers")
endif()

include(ExternalProject)
## build the rust library
ExternalProject_Add(
    shadow-rust-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} --all-targets --target-dir \"${CMAKE_CURRENT_BINARY_DIR}/target\" --features \"${RUST_FEATURES}\""
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/target/debug/libshadow_rs.a ${CMAKE_CURRENT_BINARY_DIR}/target/release/libshadow_rs.a
    INSTALL_COMMAND ""
    LOG_BUILD OFF
    DEPENDS "${RUST_DEPENDS}"
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
