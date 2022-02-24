add_linux_tests(BASENAME sleep COMMAND sh -c "../target/debug/test_sleep")
add_shadow_tests(BASENAME sleep)

# DO NOT MERGE: demo that PROCESSOR_AFFINITY does nothing
add_test(NAME taskset-affinity1 COMMAND sh -c "taskset --cpu-list -p $$")
set_property(TEST taskset-affinity1 PROPERTY PROCESSOR_AFFINITY 1)
set_property(TEST taskset-affinity1 PROPERTY PROCESSORS 1)

add_test(NAME taskset-affinity2 COMMAND sh -c "taskset --cpu-list -p $$")
set_property(TEST taskset-affinity2 PROPERTY PROCESSOR_AFFINITY 1)
set_property(TEST taskset-affinity2 PROPERTY PROCESSORS 1)

add_test(NAME taskset-noaffinity COMMAND sh -c "taskset --cpu-list -p $$")

#set_tests_properties(taskset PROPERTIES PROCESSOR_AFFINITY True)