# Make sure tests in subdirs append to previously collected tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}")

add_subdirectory(fixed_duration)
add_subdirectory(fixed_size)

# Now set the variable in the parent scope to ours, which includes subdir tests.
set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}" PARENT_SCOPE)
