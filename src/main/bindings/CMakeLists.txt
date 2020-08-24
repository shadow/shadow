add_subdirectory(c)
add_subdirectory(rust)

# A fake target that depends on the wrapper.
add_custom_target(bindings_main)
add_dependencies(bindings_main bindings_main_rust)
add_dependencies(bindings_main bindings_main_c)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings_main PROPERTY EXCLUDE_FROM_ALL true)
