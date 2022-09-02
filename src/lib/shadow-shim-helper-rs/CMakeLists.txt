rust_static_lib(shadow-shim-helper-rs FEATURES c_apis)

# Generate bindings.h in the source tree.
add_custom_command(OUTPUT shim_helper.h
    COMMAND cbindgen ${CMAKE_CURRENT_SOURCE_DIR}
        --config ${CMAKE_CURRENT_SOURCE_DIR}/cbindgen.toml
        --output ${CMAKE_CURRENT_SOURCE_DIR}/shim_helper.h)
add_custom_target(bindings_shim_helper DEPENDS shim_helper.h)
set_property(TARGET bindings_shim_helper PROPERTY EXCLUDE_FROM_ALL true)