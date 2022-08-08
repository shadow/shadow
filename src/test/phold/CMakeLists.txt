include_directories(${GLIB_INCLUDE_DIRS})
add_executable(test-phold test_phold.c)
target_link_libraries(test-phold ${M_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

# We should run tests using --use-cpu-pinning in serial, otherwise all such tests will be
# pinned to the same exact CPUs.
add_shadow_tests(
    BASENAME phold-serial
    LOGLEVEL info
    ARGS --use-cpu-pinning true
    PROPERTIES RUN_SERIAL TRUE)

# Due to --use-cpu-pinning, we run this in serial. Otherwise we should set the "PROCESSORS 2"
# property since we use 2 workers here.
add_shadow_tests(
    BASENAME phold-parallel
    LOGLEVEL info
    ARGS --use-cpu-pinning true --parallelism 2
    PROPERTIES RUN_SERIAL TRUE)

# Run tests with the round-robin queueing discipline (the current default is fifo).
# Ideally we'd want to test the different queueing displinces on a test that has a lot of
# congestion and hosts sending data on mulitple sockets at once, but phold is currently the closest
# test that we have to that configuration.
add_shadow_tests(
    BASENAME phold-rr-qdisc
    LOGLEVEL info
    ARGS --use-cpu-pinning true --interface-qdisc roundrobin
    PROPERTIES RUN_SERIAL TRUE)
