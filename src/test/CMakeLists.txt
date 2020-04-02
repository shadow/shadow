## testing for the shadow simulator
    
## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_cflags(-std=gnu99)
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
#get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher test_launcher.c test_launcher_common.c)
add_executable(shadow-test-launcher-fail test_launcher_fail.c test_launcher_common.c)

## add the test directories
# FIXME uncomment these as we get them working in Phantom.
# FIXME add_subdirectory(dynlink)
# FIXME add_subdirectory(preload)

# FIXME add_subdirectory(bind)
# FIXME add_subdirectory(cpp)
# FIXME add_subdirectory(determinism)
# FIXME add_subdirectory(epoll)
# FIXME add_subdirectory(file)
# FIXME add_subdirectory(phold)
# FIXME add_subdirectory(poll)
# FIXME add_subdirectory(pthreads)
# FIXME add_subdirectory(random)
add_subdirectory(resolver)
# FIXME add_subdirectory(shutdown)
# FIXME add_subdirectory(signal)
add_subdirectory(sleep)
# FIXME add_subdirectory(sockbuf)
# FIXME add_subdirectory(tcp)
# FIXME add_subdirectory(timerfd)
# FIXME add_subdirectory(udp)

## grep the LastTest.log.tmp file for the counter diff lines, and make sure
## that each such line matches the line in src/test/leakcheck.log
add_test(
	NAME shadow-leakcheck-grep
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-grep.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
## FIXME - eventually need all Shadow tests in the DEPENDS list
#set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "determinism1-shadow;determinism2-shadow;dynlink-shadow;preload-shadow-dl-run;preload-shadow-dl-env;bind-shadow;cpp-shadow;determinism-shadow-compare;epoll-shadow;epoll-writeable-shadow;epoll-shadow;file-shadow;phold-shadow;phold-threaded-shadow;pthreads-shadow;random-shadow;signal-shadow;sleep-shadow;sockbuf-shadow;tcp-blocking-loopback-shadow;tcp-blocking-lossless-shadow;tcp-blocking-lossy-shadow;tcp-nonblocking-poll-lossy-shadow;tcp-nonblocking-poll-lossless-shadow;tcp-nonblocking-poll-loopback-shadow;tcp-nonblocking-epoll-lossless-shadow;tcp-nonblocking-epoll-loopback-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-select-lossless-shadow;tcp-nonblocking-select-lossy-shadow;tcp-nonblocking-select-loopback-shadow;timerfd-shadow;tcp-iov-shadow")
set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "sleep-shadow-ptrace;sleep-shadow-preload")

add_test(
	NAME shadow-leakcheck-compare
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-compare.sh
        ${CMAKE_CURRENT_BINARY_DIR}/leakcheck.log
		${CMAKE_SOURCE_DIR}/src/test/leakcheck.log
)
set_tests_properties(shadow-leakcheck-compare PROPERTIES DEPENDS "shadow-leakcheck-grep")

