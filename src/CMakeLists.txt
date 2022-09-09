set(CARGO_ENV_VARS "${CARGO_ENV_VARS} SHADOW_VERSION=\"${SHADOW_VERSION_STRING_CONF}\"")

if(SHADOW_USE_PERF_TIMERS STREQUAL ON)
  set(RUST_FEATURES "${RUST_FEATURES} perf_timers")
endif()

# Propagate global C and C++ flags to the Rust build
set(RUST_CFLAGS "${CMAKE_C_FLAGS}")
set(RUST_CXXFLAGS "${CMAKE_CXX_FLAGS}")
get_property(COMPILE_OPTIONS DIRECTORY . PROPERTY COMPILE_OPTIONS)
foreach(FLAG IN LISTS COMPILE_OPTIONS COMPILE_DEFINITIONS)
  set(RUST_CFLAGS "${RUST_CFLAGS} ${FLAG}")
  set(RUST_CXXFLAGS "${RUST_CXXFLAGS} ${FLAG}")
endforeach()
get_property(COMPILE_DEFINITIONS DIRECTORY . PROPERTY COMPILE_DEFINITIONS)
foreach(DEFINITION IN LISTS COMPILE_DEFINITIONS)
  set(RUST_CFLAGS "${RUST_CFLAGS} -D${DEFINITION}")
  set(RUST_CXXFLAGS "${RUST_CXXFLAGS} -D${DEFINITION}")
endforeach()

## build the rust library
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CFLAGS=\"${RUST_CFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CXXFLAGS=\"${RUST_CXXFLAGS}\"")
set(TARGETDIR "${CMAKE_CURRENT_BINARY_DIR}/target")
include(ExternalProject)
ExternalProject_Add(
    rust-workspace-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} --target-dir \"${TARGETDIR}\" --features \"${RUST_FEATURES}\""
    BUILD_BYPRODUCTS
      ${TARGETDIR}/debug/libshadow_shim_helper_rs.a ${TARGETDIR}/release/libshadow_shim_helper_rs.a
    INSTALL_COMMAND ""
    LOG_BUILD OFF
)
foreach(LIBRARY shadow-shim-helper-rs logger shadow-shmem shadow-tsc shadow-rs)
  add_library(${LIBRARY} STATIC IMPORTED GLOBAL)
  add_dependencies(${LIBRARY} rust-workspace-project)
  string(REPLACE "-" "_" LIBRARY_FILE "lib${LIBRARY}.a")
  set_target_properties(${LIBRARY} PROPERTIES IMPORTED_LOCATION_DEBUG ${TARGETDIR}/debug/${LIBRARY_FILE})
  set_target_properties(${LIBRARY} PROPERTIES IMPORTED_LOCATION_RELEASE ${TARGETDIR}/release/${LIBRARY_FILE})
endforeach()

# Outside of external, inclusion of headers within the project should be
# relative to this directory.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)

## pthreads
set(CMAKE_THREAD_PREFER_PTHREAD 1)
find_package(Threads REQUIRED)
message(STATUS "Using Threads library: ${CMAKE_THREAD_LIBS_INIT}")

# Build support libraries.
add_subdirectory(lib)

## build the core simulator
add_subdirectory(main)

if(SHADOW_TEST STREQUAL ON)
    add_subdirectory(test)
endif(SHADOW_TEST STREQUAL ON)