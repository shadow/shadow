# Reconstruct "-I" flags.
get_target_property(INCLUDE_DIRECTORIES logger INCLUDE_DIRECTORIES)
# Add -I prefix to each directory
foreach(INCLUDE_DIRECTORY IN LISTS INCLUDE_DIRECTORIES)
    list(APPEND LLVM_FLAGS "-I${INCLUDE_DIRECTORY}")
endforeach(INCLUDE_DIRECTORY)

# Generate wrapper.rs in the source tree.
add_custom_command(OUTPUT src/bindings.rs
    COMMAND bindgen
        --whitelist-function "logger_.*"
        ${CMAKE_CURRENT_SOURCE_DIR}/wrapper.h -o ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings.rs -- ${LLVM_FLAGS}
    MAIN_DEPENDENCY wrapper.h
    IMPLICIT_DEPENDS C wrapper.h)

# A fake target that depends on the wrapper.
add_custom_target(bindings_logger DEPENDS src/bindings.rs)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings_logger PROPERTY EXCLUDE_FROM_ALL true)
