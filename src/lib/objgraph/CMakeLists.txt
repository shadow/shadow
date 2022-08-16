if(CMAKE_BUILD_TYPE STREQUAL Debug)
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
    objgraph-project
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/subtree
    BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/subtree
    BUILD_ALWAYS 1
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} -p objgraph --lib --tests --target-dir \"${CMAKE_CURRENT_BINARY_DIR}/subtree/target\" --features \"${RUST_FEATURES}\""
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/subtree/target/debug/libobjgraph.a ${CMAKE_CURRENT_BINARY_DIR}/subtree/target/release/libobjgraph.a
    INSTALL_COMMAND ""
    LOG_BUILD OFF
    DEPENDS "${RUST_DEPENDS}"
)
add_library(objgraph STATIC IMPORTED GLOBAL)
add_dependencies(objgraph objgraph-rust-project)
set_target_properties(objgraph PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/subtree/target/${RUST_BUILD_TYPE}/libobjgraph.a)

# Generate bindings.h in the source tree.
add_custom_command(OUTPUT objgraph.h
    COMMAND cbindgen ${CMAKE_CURRENT_SOURCE_DIR}/subtree
        --config ${CMAKE_CURRENT_SOURCE_DIR}/cbindgen.toml
        --output ${CMAKE_CURRENT_SOURCE_DIR}/objgraph.h)

# A fake target that depends on the wrapper.
add_custom_target(bindings_objgraph_c DEPENDS objgraph.h)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings_objgraph_c PROPERTY EXCLUDE_FROM_ALL true)