add_subdirectory(c)
add_subdirectory(rust)

# A fake target that depends on the wrapper.
add_custom_target(bindings_main)
add_dependencies(bindings_main bindings_main_c)
add_dependencies(bindings_main bindings_main_rust)

# The C bindings should be generated first since cbindgen doesn't require the Rust code
# to be valid, whereas bindgen does require the C code to be valid. If the C bindings
# are no longer correct, but the Rust bindings are generated first, then there will be
# no way to correct the C bindings since the Rust binding generation will always fail
# before the C bindings can be corrected.
add_dependencies(bindings_main_rust bindings_main_c)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings_main PROPERTY EXCLUDE_FROM_ALL true)
