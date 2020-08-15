## testing for the shadow simulator
    
## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_cflags(-std=gnu99)
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
#get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

include(ExternalProject)

## always build tests with the debug profile, even when shadow is built in release mode
set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
ExternalProject_Add(
   shadow-tests
   PREFIX ${CMAKE_CURRENT_SOURCE_DIR}
   BUILD_ALWAYS 1
   DOWNLOAD_COMMAND ""
   CONFIGURE_COMMAND ""
   SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
   BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build --target-dir ${CMAKE_CURRENT_BINARY_DIR}/target"
   INSTALL_COMMAND ""
   LOG_BUILD OFF
)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher test_launcher.c test_launcher_common.c)
add_executable(shadow-test-launcher-fail test_launcher_fail.c test_launcher_common.c)

## add the test directories
# FIXME uncomment these as we get them working in Phantom.
# FIXME add_subdirectory(dynlink)
# FIXME add_subdirectory(preload)

add_subdirectory(bind)
add_subdirectory(config)
add_subdirectory(cpp)
# FIXME add_subdirectory(determinism)
add_subdirectory(epoll)
add_subdirectory(file)
add_subdirectory(memory)
add_subdirectory(phold)
add_subdirectory(poll)
# FIXME add_subdirectory(pthreads)
add_subdirectory(random)
add_subdirectory(resolver)
add_subdirectory(shutdown)
# FIXME add_subdirectory(signal)
add_subdirectory(sleep)
add_subdirectory(sockbuf)
add_subdirectory(socket)
# FIXME add_subdirectory(tcp)
add_subdirectory(timerfd)
add_subdirectory(udp)
add_subdirectory(unistd)

## grep the LastTest.log.tmp file for the counter diff lines, and make sure
## that each such line matches the line in src/test/leakcheck.log
add_test(
	NAME shadow-leakcheck-grep
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-grep.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
## FIXME - eventually need all Shadow tests in the DEPENDS list
#set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "determinism1-shadow;determinism2-shadow;dynlink-shadow;preload-shadow-dl-run;preload-shadow-dl-env;bind-shadow;cpp-shadow;determinism-shadow-compare;epoll-shadow;epoll-writeable-shadow;epoll-shadow;file-shadow;phold-shadow;phold-threaded-shadow;pthreads-shadow;random-shadow;signal-shadow;sleep-shadow;sockbuf-shadow;tcp-blocking-loopback-shadow;tcp-blocking-lossless-shadow;tcp-blocking-lossy-shadow;tcp-nonblocking-poll-lossy-shadow;tcp-nonblocking-poll-lossless-shadow;tcp-nonblocking-poll-loopback-shadow;tcp-nonblocking-epoll-lossless-shadow;tcp-nonblocking-epoll-loopback-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-epoll-lossy-shadow;tcp-nonblocking-select-lossless-shadow;tcp-nonblocking-select-lossy-shadow;tcp-nonblocking-select-loopback-shadow;timerfd-shadow;tcp-iov-shadow")
set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "bind-shadow-preload;bind-shadow-ptrace;cpp-shadow-preload;cpp-shadow-ptrace;epoll-shadow-ptrace;epoll-writeable-shadow-ptrace;file-shadow-ptrace;mmap-shadow-ptrace;phold-shadow-ptrace;shutdown-shadow-preload;shutdown-shadow-ptrace;sleep-shadow-preload;sleep-shadow-ptrace;udp-shadow-ptrace;udp-uniprocess-shadow-preload;udp-uniprocess-shadow-ptrace;unistd-shadow-preload;unistd-shadow-ptrace")

add_test(
	NAME shadow-leakcheck-compare
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-compare.sh
        ${CMAKE_CURRENT_BINARY_DIR}/leakcheck.log
		${CMAKE_SOURCE_DIR}/src/test/leakcheck.log
)
set_tests_properties(shadow-leakcheck-compare PROPERTIES DEPENDS "shadow-leakcheck-grep")

