add_test(NAME sendto-recvfrom COMMAND ../../target/debug/test_sendto_recvfrom --libc-passing)
list(APPEND TestNames sendto-recvfrom)

foreach(Method ptrace preload)
    # runs the shadow tests with only the passing tests, not all tests
    add_test(NAME sendto-recvfrom-shadow-${Method} COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${Method}
        -l debug -d sendto-recvfrom.shadow-${Method}.data ${CMAKE_CURRENT_SOURCE_DIR}/sendto-recvfrom.shadow.config.xml)
    list(APPEND TestNames sendto-recvfrom-shadow-${Method})
endforeach()

set_property(TEST ${TestNames} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
# need to check the test's return code, not just shadow's (see shadow/shadow#902)
set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")

# This test fails in shadow (debug mode) on centos 7 in the GitHub CI. The
# cause of failure seems to be that centos 7 is about twice as slow as the
# other distros, and debug mode causes so many debug log messages that it
# exceeds the default timeout of 30 seconds. The test takes about 3 seconds
# on a laptop with SSD. Due to shadow's small `SYSCALL_IO_BUFSIZE` size of
# 16 KiB, the 'test_nonblocking_tcp' test needs to make a lot of
# sendto()/recvfrom() calls before an EAGAIN is returned.
set_property(TEST ${TestNames} PROPERTY TIMEOUT 60)
