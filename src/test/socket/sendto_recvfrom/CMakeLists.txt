add_linux_tests(BASENAME sendto-recvfrom COMMAND sh -c "../../target/debug/test_sendto_recvfrom --libc-passing")
add_shadow_tests(BASENAME sendto-recvfrom METHODS ptrace-only ptrace preload)

# This test fails in shadow (debug mode) on centos 7 in the GitHub CI. The
# cause of failure seems to be that centos 7 is about twice as slow as the
# other distros, and debug mode causes so many debug log messages that it
# exceeds the default timeout of 30 seconds. The test takes about 3 seconds
# on a laptop with SSD. Due to shadow's small `SYSCALL_IO_BUFSIZE` size of
# 16 KiB, the 'test_nonblocking_tcp' test needs to make a lot of
# sendto()/recvfrom() calls before an EAGAIN is returned.
set_property(TEST 
                sendto-recvfrom-shadow-ptrace-only 
                sendto-recvfrom-shadow-ptrace 
                sendto-recvfrom-shadow-preload 
            PROPERTY TIMEOUT 60)
