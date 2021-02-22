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
   PREFIX ${CMAKE_CURRENT_BINARY_DIR}
   BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
   BUILD_ALWAYS 1
   DOWNLOAD_COMMAND ""
   CONFIGURE_COMMAND ""
   BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build"
   INSTALL_COMMAND ""
   LOG_BUILD OFF
)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher test_launcher.c test_launcher_common.c)
add_executable(shadow-test-launcher-fail test_launcher_fail.c test_launcher_common.c)

## Wrapper around the yaml2xml tool
set (yaml2xml "python3 ${CMAKE_SOURCE_DIR}/src/tools/convert.py yaml2xml")

## === Helper macros for consistently defining tests. ===
## Tests that need more features should use add_test directly.

## example: add_linux_tests(BASENAME bind COMMAND test_bind)
## will create one test named bind-linux
macro(add_linux_tests)
   cmake_parse_arguments(LINUX_TEST "" "BASENAME" "COMMAND" ${ARGN})
   add_test(NAME ${LINUX_TEST_BASENAME}-linux COMMAND ${LINUX_TEST_COMMAND})
   set_property(TEST ${LINUX_TEST_BASENAME}-linux PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")
endmacro()

## example: add_shadow_tests(BASENAME bind METHODS ptrace preload LOGLEVEL debug ARGS --pin-cpus)
## will create two tests named bind-shadow-ptrace and bind-shadow-preload
macro(add_shadow_tests)
   cmake_parse_arguments(SHADOW_TEST "" "BASENAME;LOGLEVEL" "METHODS;ARGS;CONFIGURATIONS" ${ARGN})

   if(NOT DEFINED SHADOW_TEST_LOGLEVEL)
      set(SHADOW_TEST_LOGLEVEL "debug")
   endif()
   
   string(REPLACE ";" " " SHADOW_TEST_ARGS "${SHADOW_TEST_ARGS}")

   foreach(SHADOW_TEST_METHOD ${SHADOW_TEST_METHODS})
      set(SHADOW_TEST_NAME ${SHADOW_TEST_BASENAME}-shadow-${SHADOW_TEST_METHOD})
      
      set(SHADOW_TEST_COMMAND sh -c "\
         ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADOW_TEST_BASENAME}.yaml --output - \
         | ${CMAKE_BINARY_DIR}/src/main/shadow \
         --data-directory=${SHADOW_TEST_NAME}.data \
         --interpose-method=${SHADOW_TEST_METHOD} \
         --log-level=${SHADOW_TEST_LOGLEVEL} \
         ${SHADOW_TEST_ARGS} \
         - \
         "
      )
      
      if(DEFINED SHADOW_TEST_CONFIGURATIONS)
         add_test(
            NAME ${SHADOW_TEST_NAME}
            COMMAND ${SHADOW_TEST_COMMAND}
            CONFIGURATIONS ${SHADOW_TEST_CONFIGURATIONS}
         )
      else()
         add_test(
            NAME ${SHADOW_TEST_NAME}
            COMMAND ${SHADOW_TEST_COMMAND}
         )
      endif()

      # need to check the test's return code, not just shadow's (see shadow/shadow#902)
      set_property(TEST ${SHADOW_TEST_NAME} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
      set_property(TEST ${SHADOW_TEST_NAME} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")

      set(ALL_SHADOW_TESTS "${ALL_SHADOW_TESTS}" PARENT_SCOPE)
      list(APPEND ALL_SHADOW_TESTS ${SHADOW_TEST_NAME})
   endforeach()
endmacro()
## === end test helper macros ===

## add the test directories
# FIXME uncomment these as we get them working in Phantom.
# FIXME add_subdirectory(dynlink)
# FIXME add_subdirectory(preload)

add_subdirectory(bind)
add_subdirectory(clone)
add_subdirectory(config)
add_subdirectory(cpp)
add_subdirectory(determinism)
add_subdirectory(epoll)
add_subdirectory(eventfd)
add_subdirectory(file)
add_subdirectory(futex)
add_subdirectory(ifaddrs)
add_subdirectory(memory)
add_subdirectory(phold)
add_subdirectory(pipe)
add_subdirectory(poll)
add_subdirectory(pthreads)
add_subdirectory(random)
add_subdirectory(resolver)
add_subdirectory(signal)
add_subdirectory(sleep)
add_subdirectory(sockbuf)
add_subdirectory(socket)
add_subdirectory(tcp)
add_subdirectory(timerfd)
add_subdirectory(udp)
add_subdirectory(unistd)

message(STATUS "ALL_SHADOW_TESTS = ${ALL_SHADOW_TESTS}")

## grep the LastTest.log.tmp file for the counter diff lines, and make sure
## that each such line matches the line in src/test/leakcheck.log
add_test(
	NAME leakcheck-shadow-grep
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-grep.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# List all shadow tests as dependencies, so that the leakcheck can grep their output for leaks.
## TODO swap the following lines when all tests have been converted to the macros
#set_tests_properties(leakcheck-shadow-grep PROPERTIES DEPENDS "${ALL_SHADOW_TESTS}")
set_tests_properties(leakcheck-shadow-grep PROPERTIES DEPENDS "bind-shadow-ptrace;bind-shadow-preload;clone-shadow-ptrace;config-convert-shadow;cpp-shadow-ptrace;cpp-shadow-preload;epoll-shadow-ptrace;epoll-writeable-shadow-ptrace;eventfd-shadow-ptrace;eventfd-shadow-preload;file-shadow-ptrace;mmap-shadow-ptrace;phold-shadow-ptrace;phold-threaded-shadow-ptrace;random-shadow-ptrace;random-shadow-preload;shutdown-shadow-ptrace;shutdown-shadow-preload;sleep-shadow-ptrace;sleep-shadow-preload;sockbuf-shadow-ptrace;socket-shadow-ptrace;socket-shadow-preload;bind-shadow-ptrace;bind-shadow-preload;listen-shadow-ptrace;listen-shadow-preload;getsockname-shadow-ptrace;getsockname-shadow-preload;accept-shadow-ptrace;accept-shadow-preload;timerfd-shadow-ptrace;udp-uniprocess-shadow-ptrace;udp-uniprocess-shadow-preload;udp-shadow-ptrace;unistd-shadow-ptrace;unistd-shadow-preload")

add_test(
	NAME leakcheck-shadow-compare
	COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/src/test/leakcheck-compare.sh
        ${CMAKE_CURRENT_BINARY_DIR}/leakcheck.log
		${CMAKE_SOURCE_DIR}/src/test/leakcheck.log
)
set_tests_properties(leakcheck-shadow-compare PROPERTIES DEPENDS "leakcheck-shadow-grep")