add_executable(test-config-read-from-stdin test_config.c)
add_shadow_tests(BASENAME config-stdin METHODS hybrid ptrace preload)