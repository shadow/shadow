include(ExternalProject)

set(RUSTFLAGS "-C force-frame-pointers=y")
set(CARGO_ENV_VARS "SHADOW_VERSION=\"${SHADOW_VERSION_STRING_CONF}\"")

if(SHADOW_USE_PERF_TIMERS STREQUAL ON)
  set(RUST_FEATURES "${RUST_FEATURES} perf_timers")
endif()

if(SHADOW_EXTRA_TESTS STREQUAL ON)
  set(TEST_FEATURES "${TEST_FEATURES} extra_tests")
endif()

if(SHADOW_WERROR STREQUAL ON)
  set(RUSTFLAGS "${RUSTFLAGS} -Dwarnings")
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

# don't build unit tests since they'll have to be rebuilt when we run the 'rust-unit-tests' test anyways
set(RUST_TARGETS "--lib --bins")

## build the rust library
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CFLAGS=\"${RUST_CFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CXXFLAGS=\"${RUST_CXXFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CC=\"${CMAKE_C_COMPILER}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CXX=\"${CMAKE_CXX_COMPILER}\"")
set(TARGETDIR "${CMAKE_CURRENT_BINARY_DIR}/target")
ExternalProject_Add(
    rust-workspace-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    # cmake appears to truncate strings when there's a semicolon,
    # so we need to write this awkwardly
    BUILD_COMMAND bash -c " \
        VERSION=\"$(${CARGO_ENV_VARS} rustc --version | awk '{print $2}')\" \
        && [[ \"$VERSION\" == 1.89.0 ]] \
        && echo '' \
        && echo \"Unsupported rust version: $VERSION\" \
        && echo 'See https://github.com/shadow/shadow/issues/3654' \
        && echo '' \
        && exit 1 || exit 0 \
        "
    COMMAND echo "Building workspace..."
    COMMAND bash -c " \
        ${CARGO_ENV_VARS} \
        cargo build \
        --workspace --exclude shadow-tests --exclude shadow-shim \
        ${RUST_BUILD_FLAG} \
        ${RUST_TARGETS} \
        --target-dir \"${TARGETDIR}\" \
        --features \"${RUST_FEATURES}\" \
        "
    COMMAND echo "Building shadow-tests..."
    # Always build shadow-tests with the debug profile, even when shadow is built in release mode.
    # TODO: we should build tests without optimizations, which are automatically enabled for
    # debug builds due to the 'opt-level' option in the workspace.
    # Omit RUST_FEATURES, since the shadow-tests package doesn't define any of the optional features
    # that we trigger via this wrapper.
    COMMAND bash -c " \
        ${CARGO_ENV_VARS} \
        cargo build \
        --package shadow-tests \
        --target-dir \"${TARGETDIR}\" \
        --features \"${TEST_FEATURES}\" \
        "
    BUILD_BYPRODUCTS
      ${TARGETDIR}/debug/libshadow_shim_helper_rs.a ${TARGETDIR}/release/libshadow_shim_helper_rs.a
    INSTALL_COMMAND ""
    LOG_BUILD OFF
)
foreach(LIBRARY shadow-shim-helper-rs logger shadow-shmem asm-util shadow-rs)
  add_library(${LIBRARY} STATIC IMPORTED GLOBAL)
  add_dependencies(${LIBRARY} rust-workspace-project)
  string(REPLACE "-" "_" LIBRARY_FILE "lib${LIBRARY}.a")
  set_target_properties(${LIBRARY} PROPERTIES IMPORTED_LOCATION_DEBUG ${TARGETDIR}/debug/${LIBRARY_FILE})
  set_target_properties(${LIBRARY} PROPERTIES IMPORTED_LOCATION_RELEASE ${TARGETDIR}/release/${LIBRARY_FILE})
  # Rust's std needs -ldl and -lpthread
  target_link_libraries(${LIBRARY} INTERFACE dl pthread)
endforeach()

# Unit test executables don't have predictable names: https://github.com/rust-lang/cargo/issues/1924
#
# We previously looked for executables with names matching '${UNDERBARRED_CRATENAME}*' in
# `${TARGETDIR}/${RUST_BUILD_TYPE}/deps/`, but this would require us to enumerate all the crates
# with unit tests, which would be particularly error prone now that we're not individually building
# the crates from cmake. (This is a larger set than the library-list above, which are only
# the crates that we need to link from C code)
#
# Just using `cargo test` for now. It potentially (re)builds code, but seems
# better than trying to fight the "rust way" here. It also includes doc tests,
# which normally don't have persistent executables at all.
add_test(NAME rust-unit-tests
         COMMAND bash -c " \
            ${CARGO_ENV_VARS} \
            cargo test \
            ${RUST_BUILD_FLAG} \
            --manifest-path \"${CMAKE_CURRENT_SOURCE_DIR}\"/Cargo.toml \
            --target-dir \"${TARGETDIR}\" \
            --features \"${RUST_FEATURES}\" \
            "
         )
# Longer timeout here and run serially, since it's running a whole test suite,
# and may end up rebuilding code if anything has changed.
set_tests_properties(rust-unit-tests PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)

# We need a separate project to build the shim, for several reasons:
# * We need to pass extra RUSTFLAGS that only make sense for this target
# * When we make the shim no_std, it probably can't be a member of the
#   top-level workspace, unless we change everything to abort on panic.
# * When we make the shim no_std, we'll want all dependencies built with
#   only the features that the shim asks for, so that another crate
#   doesn't end up enabling a feature that brings in `std`.

# We need to set the soname of the shim for the injector lib to link to it correctly.
# See https://github.com/rust-lang/rust/pull/31170.
# We can't just set it in the shim's [build].rustflags, because that gets ignored
# completely if the RUSTFLAGS env variable is set.
# https://doc.rust-lang.org/cargo/reference/config.html#buildrustflags
set(RUSTFLAGS "$RUSTFLAGS -C link_args=-Wl,-soname,libshadow_shim.so")
set(CARGO_ENV_VARS "")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CFLAGS=\"${RUST_CFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CXXFLAGS=\"${RUST_CXXFLAGS}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CC=\"${CMAKE_C_COMPILER}\"")
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} CXX=\"${CMAKE_CXX_COMPILER}\"")
# Use a separate targetdir so that this and "main" rust build
# don't invalidate eachother's build caches.
# TODO: I'm not sure *why* they invalidate each-other's build caches.
# Theoretically cargo is supposed to avoid that sort of issue by mangling
# the compiler artifacts with the appropriate metadata to be able to keep
# multiple configurations around at once.
set(TARGETDIR "${CMAKE_CURRENT_BINARY_DIR}/shim/target")
ExternalProject_Add(
    rust-shadow-shim-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND bash -c " \
        ${CARGO_ENV_VARS} \
        cargo build \
        --package shadow-shim \
        ${RUST_BUILD_FLAG} \
        --target-dir \"${TARGETDIR}\" \
        --features \"${RUST_FEATURES}\" \
        "
    BUILD_BYPRODUCTS
      ${TARGETDIR}/debug/libshadow_shim.so ${TARGETDIR}/release/libshadow_shim.so
    INSTALL_COMMAND ""
    LOG_BUILD OFF
)
# build the shim after shadow (cargo already parallelizes the build)
add_dependencies(rust-shadow-shim-project rust-workspace-project)
add_library(shadow-shim SHARED IMPORTED GLOBAL)
add_dependencies(shadow-shim rust-shadow-shim-project)
set_target_properties(shadow-shim PROPERTIES IMPORTED_LOCATION_DEBUG ${TARGETDIR}/debug/libshadow_shim.so)
set_target_properties(shadow-shim PROPERTIES IMPORTED_LOCATION_RELEASE ${TARGETDIR}/release/libshadow_shim.so)
# Use this instead when we're able to bump the cmake min version to >= 3.21:
# > install(IMPORTED_RUNTIME_ARTIFACTS shadow-shim LIBRARY DESTINATION lib)
install(FILES ${TARGETDIR}/debug/libshadow_shim.so DESTINATION lib CONFIGURATIONS Debug)
install(FILES ${TARGETDIR}/release/libshadow_shim.so DESTINATION lib CONFIGURATIONS Release)

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
