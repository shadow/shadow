set(LIBFILE libshadow_shim_helper_rs.a)
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
include(ExternalProject)
## build the rust library
ExternalProject_Add(
    shadow-shim-helper-rs-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} --all-targets --target-dir \"${CMAKE_CURRENT_BINARY_DIR}/target\" --features \"${RUST_FEATURES}\""
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/target/debug/${LIBFILE} ${CMAKE_CURRENT_BINARY_DIR}/target/release/${LIBFILE}
    INSTALL_COMMAND ""
    LOG_BUILD OFF
    DEPENDS
)
add_library(shadow-shim-helper-rs STATIC IMPORTED GLOBAL)
add_dependencies(shadow-shim-helper-rs shadow-shim-helper-rs-project)
set_target_properties(shadow-shim-helper-rs PROPERTIES IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/target/debug/${LIBFILE})
set_target_properties(shadow-shim-helper-rs PROPERTIES IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/target/release/${LIBFILE})

## we can't predict exact executable names until this is fixed: https://github.com/rust-lang/cargo/issues/1924
add_test(NAME shadow-shim-helper-rust-unit-tests COMMAND bash -c "exec \"$(find ${CMAKE_CURRENT_BINARY_DIR}/target/${RUST_BUILD_TYPE}/deps/ \
                                               -type f -executable -name 'shadow_shim_helper_rs-*' -print | head -n 1)\" --color always")
set_property(TEST shadow-shim-helper-rust-unit-tests PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")

# Generate bindings.h in the source tree.
add_custom_command(OUTPUT shim_helper.h
    COMMAND cbindgen ${CMAKE_CURRENT_SOURCE_DIR}
        --config ${CMAKE_CURRENT_SOURCE_DIR}/cbindgen.toml
        --output ${CMAKE_CURRENT_SOURCE_DIR}/shim_helper.h)
add_custom_target(bindings_shim_helper DEPENDS shim_helper.h)
set_property(TARGET bindings_shim_helper PROPERTY EXCLUDE_FROM_ALL true)