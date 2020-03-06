## testing for the shadow simulator
    
## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_cflags(-std=gnu99)
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
#get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher shd-test-launcher.c shd-test-launcher-common.c)
add_executable(shadow-test-launcher-fail shd-test-launcher-fail.c shd-test-launcher-common.c)

## add the test directories
add_subdirectory(dynlink)
add_subdirectory(preload)

add_subdirectory(bind)
add_subdirectory(cpp)
add_subdirectory(determinism)
add_subdirectory(epoll)
add_subdirectory(file)
add_subdirectory(phold)
add_subdirectory(poll)
add_subdirectory(pthreads)
add_subdirectory(random)
add_subdirectory(shutdown)
add_subdirectory(signal)
add_subdirectory(sleep)
add_subdirectory(sockbuf)
add_subdirectory(tcp)
add_subdirectory(timerfd)

## FIXME - the LastTest.log.tmp file does not contain all output when we do
## the grep above, so we get an inconsistent number of results in the output.
## For now, we just check the first 20 results so that some checking is in place.
add_test(
	NAME shadow-leakcheck-grep
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "determinism1-shadow;determinism2-shadow;dynlink-shadow;preload-shadow-dl-run;preload-shadow-dl-env;bind-shadow;cpp-shadow;determinism-shadow-compare;epoll-shadow;epoll-writeable-shadow;epoll-shadow;file-shadow;phold-shadow;phold-threaded-shadow;pthreads-shadow;random-shadow;signal-shadow;sleep-shadow;sockbuf-shadow;tcp-blocking-loopback-shadow;tcp-blocking-lossless-shadow;tcp-blocking-lossy-shadow;tcp-nonblocking-poll-lossy-shadow;tcp-nonblocking-poll-lossless-shadow;tcp-nonblocking-poll-loopback-shadow;tcp-nonblocking-epoll-lossless-shadow;tcp-nonblocking-epoll-loopback-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-select-lossless-shadow;tcp-nonblocking-select-lossy-shadow;tcp-nonblocking-select-loopback-shadow;timerfd-shadow;tcp-iov-shadow")

add_test(
    NAME shadow-leakcheck-compare
    COMMAND ${CMAKE_COMMAND} -E compare_files 
		${CMAKE_CURRENT_BINARY_DIR}/leakcheck.log
		${CMAKE_SOURCE_DIR}/src/test/leakcheck.log
)
set_tests_properties(shadow-leakcheck-compare PROPERTIES DEPENDS "shadow-leakcheck-grep")
