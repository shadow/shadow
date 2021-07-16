## TEST 1 (Basic tests)

## build the test as a dynamic executable that plugs into shadow
add_executable(test-determinism test_determinism.c)
target_link_libraries(test-determinism ${CMAKE_THREAD_LIBS_INIT})

# preload doesn't support threads.
foreach(METHOD ptrace preload)
    ## We need to run twice to make sure the 'random' output is the same both times
    add_shadow_tests(
        BASENAME determinism1a
        METHODS ${METHOD}
        SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml
        ARGS --use-cpu-pinning true --parallelism 2
        PROPERTIES RUN_SERIAL TRUE)
    add_shadow_tests(
        BASENAME determinism1b
        METHODS ${METHOD}
        SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism1.test.shadow.config.yaml
        ARGS --use-cpu-pinning true --parallelism 2
        PROPERTIES RUN_SERIAL TRUE)
    add_test(
        NAME determinism1-compare-shadow-${METHOD}
        COMMAND ${CMAKE_COMMAND} -D METHOD=${METHOD} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism1_compare.cmake)
    set_tests_properties(determinism1-compare-shadow-${METHOD}
        PROPERTIES DEPENDS "determinism1a-shadow-${METHOD};determinism1b-shadow-${METHOD}")
endforeach()

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
foreach(METHOD ptrace preload)
    add_shadow_tests(
        BASENAME determinism2a
        METHODS ${METHOD}
        SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml
        ARGS --use-cpu-pinning true --parallelism 2
        PROPERTIES RUN_SERIAL TRUE)
    add_shadow_tests(
        BASENAME determinism2b
        METHODS ${METHOD}
        SHADOW_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/determinism2.test.shadow.config.yaml
        ARGS --use-cpu-pinning true --parallelism 2
        PROPERTIES RUN_SERIAL TRUE)
    ## Now compare the output
    add_test(
        NAME determinism2-shadow-${METHOD}-compare
        COMMAND ${CMAKE_COMMAND} -D METHOD=${METHOD} -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism2_compare.cmake)
    ## Make sure the tests that produce output finish before we compare the output,
    ## and make sure the test-phold binary was already built, because this test uses it.
    set_tests_properties(determinism2-shadow-${METHOD}-compare
        PROPERTIES DEPENDS "determinism2a-shadow-${METHOD};determinism2b-shadow-${METHOD};test-phold")
endforeach()

## copy the file to the build test dir so that the relative path to it is correct
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights.txt ${CMAKE_CURRENT_BINARY_DIR}/weights.txt COPYONLY)

