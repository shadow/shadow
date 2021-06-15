# Make sure tests in subdirs append to previously collected tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}")

add_subdirectory(minimal)

# Now set the variable in the parent scope to ours, which includes subdir tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}" PARENT_SCOPE)
