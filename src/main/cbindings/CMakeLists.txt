# Reconstruct relevant flags to pass through to LLVM for header parsing.
# Unfortunately CMAKE_C_FLAGS doesn't include everything; we need to add
# more, below.
separate_arguments(LLVM_FLAGS UNIX_COMMAND "${CMAKE_C_FLAGS}")

# Reconstruct "-I" flags.
get_target_property(INCLUDE_DIRECTORIES shadow-c INCLUDE_DIRECTORIES)
# Add -I prefix to each directory
foreach(INCLUDE_DIRECTORY IN LISTS INCLUDE_DIRECTORIES)
    list(APPEND LLVM_FLAGS "-I${INCLUDE_DIRECTORY}")
endforeach(INCLUDE_DIRECTORY)

# Reconstruct "-D" flags.
get_directory_property(COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
# Add -D prefix to each definition
foreach(COMPILE_DEFINITION IN LISTS COMPILE_DEFINITIONS)
    list(APPEND LLVM_FLAGS "-D${COMPILE_DEFINITION}")
endforeach(COMPILE_DEFINITION)

# Generate wrapper.rs in the source tree.
add_custom_command(OUTPUT wrapper.rs
    COMMAND bindgen ${CMAKE_CURRENT_SOURCE_DIR}/wrapper.h -o ${CMAKE_CURRENT_SOURCE_DIR}/wrapper.rs -- ${LLVM_FLAGS}
    MAIN_DEPENDENCY wrapper.h
    IMPLICIT_DEPENDS C wrapper.h)

# A fake target that depends on the wrapper.
add_custom_target(bindgen_main DEPENDS wrapper.rs)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindgen_main PROPERTY EXCLUDE_FROM_ALL true)
