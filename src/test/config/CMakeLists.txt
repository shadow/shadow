# Make sure tests in subdirs append to previously collected tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}")

# disabled until we update the conversion script for the latest yaml format
add_subdirectory(convert)
add_subdirectory(read_from_stdin)

# Now set the variable in the parent scope to ours, which includes subdir tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}" PARENT_SCOPE)
