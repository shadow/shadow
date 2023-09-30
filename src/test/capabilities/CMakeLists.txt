include_directories(${GLIB_INCLUDE_DIRS})
link_libraries(${GLIB_LIBRARIES})

# We put all the targets in the extra configuration because they require libcap-dev

add_executable(test-capget test_capget.c)
set_property(TARGET test-capget PROPERTY EXCLUDE_FROM_ALL true)
add_dependencies(extra_tests test-capget)

add_shadow_tests(
    BASENAME capget
    CONFIGURATIONS extra)

add_executable(test-capset test_capset.c)
set_property(TARGET test-capset PROPERTY EXCLUDE_FROM_ALL true)
add_dependencies(extra_tests test-capset)

add_linux_tests(
    BASENAME capset
    COMMAND test-capset
    CONFIGURATIONS extra)
add_shadow_tests(
    BASENAME capset
    CONFIGURATIONS extra)
