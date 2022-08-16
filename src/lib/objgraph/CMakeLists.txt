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