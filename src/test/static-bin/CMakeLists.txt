# this requires the 'glibc-static' package on fedora, so we make it an extra test
add_custom_target(nop ALL COMMAND rustc -C "target-feature=+crt-static" "${CMAKE_CURRENT_SOURCE_DIR}/nop.rs")
set_property(TARGET nop PROPERTY EXCLUDE_FROM_ALL true)
add_dependencies(extra_tests nop)

# cmake will ignore the exit code and only search for the regex
add_shadow_tests(BASENAME static-bin PROPERTIES PASS_REGULAR_EXPRESSION "not a dynamically linked ELF" CONFIGURATIONS extra)
