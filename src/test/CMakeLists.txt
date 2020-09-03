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
add_subdirectory(clone)
add_subdirectory(config)
add_subdirectory(cpp)
# FIXME add_subdirectory(determinism)
add_subdirectory(epoll)
add_subdirectory(file)
add_subdirectory(memory)
add_subdirectory(multihost)
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
# All shadow test targets should be listed as dependencies, so that the leakcheck can grep their output for leaks.
# Generate with: `(cd build ; ctest -R shadow -E leakcheck -N | head --lines=-2 | tail --lines=+2 | awk '{printf $3 ";"}')`
set_tests_properties(shadow-leakcheck-grep PROPERTIES DEPENDS "bind-shadow-ptrace;bind-shadow-preload;clone-shadow-ptrace;config-convert-shadow;cpp-shadow-ptrace;cpp-shadow-preload;epoll-shadow-ptrace;epoll-writeable-shadow-ptrace;file-shadow-ptrace;mmap-shadow-ptrace;phold-shadow-ptrace;phold-threaded-shadow-ptrace;random-shadow-ptrace;random-shadow-preload;shutdown-shadow-ptrace;shutdown-shadow-preload;sleep-shadow-ptrace;sleep-shadow-preload;sockbuf-shadow-ptrace;socket-shadow-ptrace;socket-shadow-preload;bind-shadow-ptrace;bind-shadow-preload;listen-shadow-ptrace;listen-shadow-preload;getsockname-shadow-ptrace;getsockname-shadow-preload;accept-shadow-ptrace;accept-shadow-preload;timerfd-shadow-ptrace;udp-uniprocess-shadow-ptrace;udp-uniprocess-shadow-preload;udp-shadow-ptrace;unistd-shadow-ptrace;unistd-shadow-preload")

add_test(
	NAME shadow-leakcheck-compare
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-compare.sh
        ${CMAKE_CURRENT_BINARY_DIR}/leakcheck.log
		${CMAKE_SOURCE_DIR}/src/test/leakcheck.log
)
set_tests_properties(shadow-leakcheck-compare PROPERTIES DEPENDS "shadow-leakcheck-grep")

