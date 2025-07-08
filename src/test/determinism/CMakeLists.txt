## TEST 1 (Basic tests)

## We need to run twice to make sure the 'random' output is the same both times
add_shadow_tests(
    BASENAME determinism1a
    LOGLEVEL debug
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml
    ARGS --use-cpu-pinning true --parallelism 2 --strace-logging-mode deterministic
    PROPERTIES RUN_SERIAL TRUE)
add_shadow_tests(
    BASENAME determinism1b
    LOGLEVEL debug
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml
    ARGS --use-cpu-pinning true --parallelism 2 --strace-logging-mode deterministic
    PROPERTIES RUN_SERIAL TRUE)
add_test(
    NAME determinism1-compare-shadow
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism1_compare.cmake)
set_tests_properties(determinism1-compare-shadow
    PROPERTIES DEPENDS "determinism1a-shadow;determinism1b-shadow")

## TEST 2 (Extended packet tests)

## TODO
## We should also split the shadow.log file into a separate file for each host
## and then grep each file for "packet_add", and do diffs on the output of that.
## That way we could check the order of every packet event at every processing point.
## For example:
## for i in {1..10}
## do
##     cat shadow-a.log | grep "peer${i}:" | grep "packet_add" | cut -d' ' -f3- > peer${i}-a.log
##     cat shadow-b.log | grep "peer${i}:" | grep "packet_add" | cut -d' ' -f3- > peer${i}-b.log
##     diff --brief peer${i}-a.log peer${i}-b.log
## done

## now let's run a phold test and compare the order of packet events
add_shadow_tests(
    BASENAME determinism2a
    LOGLEVEL debug
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml
    ARGS --use-cpu-pinning true --parallelism 2 --strace-logging-mode deterministic --scheduler thread-per-host
    PROPERTIES RUN_SERIAL TRUE)
add_shadow_tests(
    BASENAME determinism2b
    LOGLEVEL debug
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml
    ARGS --use-cpu-pinning true --parallelism 2 --strace-logging-mode deterministic --scheduler thread-per-host
    PROPERTIES RUN_SERIAL TRUE)

## also test with a different scheduler
add_shadow_tests(
    BASENAME determinism2c
    LOGLEVEL debug
    SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml
    ARGS --use-cpu-pinning true --parallelism 2 --strace-logging-mode deterministic --scheduler thread-per-core
    PROPERTIES RUN_SERIAL TRUE)

## Now compare the output
add_test(
    NAME determinism2-shadow-compare
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism2_compare.cmake)

## Make sure the tests that produce output finish before we compare the output,
## and make sure the test-phold binary was already built, because this test uses it.
set_tests_properties(determinism2-shadow-compare
    PROPERTIES DEPENDS "determinism2a-shadow;determinism2b-shadow;determinism2c-shadow;test-phold")

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)
