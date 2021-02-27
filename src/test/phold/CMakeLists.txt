include_directories(${GLIB_INCLUDES})
add_executable(test-phold test_phold.c)
target_link_libraries(test-phold ${M_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

# FIXME: Enable the preload tests in all configurations. See https://github.com/shadow/shadow/issues/892

# We should run tests using --pin-cpus in serial, otherwise all such tests will be 
# pinned to the same exact CPUs.
add_shadow_tests(
    BASENAME phold-serial
    METHODS hybrid ptrace
    LOGLEVEL info
    ARGS --pin-cpus
    PROPERTIES RUN_SERIAL TRUE)
add_shadow_tests(
    BASENAME phold-serial
    METHODS preload
    LOGLEVEL info
    ARGS --pin-cpus
    PROPERTIES RUN_SERIAL TRUE
    CONFIGURATIONS ilibc)

# Due to --pin-cpus, we run this in serial. Otherwise we should set the "PROCESSORS 2"
# property since we use 2 workers here.
add_shadow_tests(
    BASENAME phold-parallel
    METHODS hybrid ptrace
    LOGLEVEL info
    ARGS --pin-cpus --workers 2
    PROPERTIES RUN_SERIAL TRUE)
add_shadow_tests(
    BASENAME phold-parallel
    METHODS preload
    LOGLEVEL info
    ARGS --pin-cpus --workers 2
    PROPERTIES RUN_SERIAL TRUE
    CONFIGURATIONS ilibc)