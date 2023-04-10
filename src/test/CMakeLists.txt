## testing for the shadow simulator

add_compile_options(-ggdb)
add_compile_options(-fno-omit-frame-pointer)
add_compile_options(-Wreturn-type)
add_compile_options(-Wswitch)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    # we only check for unused functions when builing in debug mode since some
    # functions are only called when logging, which can be #ifdef'd out in
    # release mode
    add_compile_options(-Wunused-function)
endif()

## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_cflags(-std=gnu99)
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
#get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher test_launcher.c test_launcher_common.c)
add_executable(shadow-test-launcher-fail test_launcher_fail.c test_launcher_common.c)

## === Helper macros for consistently defining tests. ===
## Tests that need more features should use add_test directly.

## example: add_linux_tests(BASENAME bind COMMAND test_bind)
## will create one test named bind-linux
macro(add_linux_tests)
   cmake_parse_arguments(LINUX_TEST "" "BASENAME" "COMMAND;CONFIGURATIONS;PROPERTIES" ${ARGN})
   if(DEFINED LINUX_TEST_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "Unrecognized arguments: ${LINUX_TEST_UNPARSED_ARGUMENTS}")
   endif()

   set(LINUX_TEST_NAME ${LINUX_TEST_BASENAME}-linux)
   if(DEFINED LINUX_TEST_CONFIGURATIONS)
      add_test(
         NAME ${LINUX_TEST_NAME}
         COMMAND ${LINUX_TEST_COMMAND}
         CONFIGURATIONS ${LINUX_TEST_CONFIGURATIONS}
         )
   else()
      add_test(
         NAME ${LINUX_TEST_NAME}
         COMMAND ${LINUX_TEST_COMMAND})
   endif()
   set_property(TEST ${LINUX_TEST_NAME} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1;G_DEBUG=fatal-criticals")
   if(DEFINED LINUX_TEST_PROPERTIES)
      set_tests_properties(${LINUX_TEST_NAME} PROPERTIES ${LINUX_TEST_PROPERTIES})
   endif()
   set_property(TEST ${LINUX_TEST_NAME} APPEND PROPERTY LABELS linux)
endmacro()

## example: add_shadow_tests(BASENAME bind LOGLEVEL debug ARGS --pin-cpus)
## will create a test named bind-shadow
macro(add_shadow_tests)
   cmake_parse_arguments(SHADOW_TEST "" "BASENAME;LOGLEVEL;SHADOW_CONFIG;CHECK_RETVAL;POST_CMD;EXPECT_ERROR" "ARGS;CONFIGURATIONS;PROPERTIES" ${ARGN})
   if(DEFINED SHADOW_TEST_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "Unrecognized arguments: ${SHADOW_TEST_UNPARSED_ARGUMENTS}")
   endif()

   if(NOT DEFINED SHADOW_TEST_LOGLEVEL)
      if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
         set(SHADOW_TEST_LOGLEVEL "trace")
      else()
         set(SHADOW_TEST_LOGLEVEL "info")
      endif()
   endif()

   if(NOT DEFINED SHADOW_TEST_SHADOW_CONFIG)
       set(SHADOW_TEST_SHADOW_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/${SHADOW_TEST_BASENAME}.yaml")
   endif()

   if(NOT DEFINED SHADOW_TEST_CHECK_RETVAL)
      set(SHADOW_TEST_CHECK_RETVAL TRUE)
   endif()

   if(NOT DEFINED SHADOW_TEST_EXPECT_ERROR)
      set(SHADOW_TEST_EXPECT_ERROR FALSE)
   endif()

   if(SHADOW_TEST_EXPECT_ERROR)
      set(INVERT_ERROR_CODE "!")
   else()
      set(INVERT_ERROR_CODE "")
   endif()

   # If cpu pinning is not explicitly set, we disable it so we avoid pinning
   # multiple tests that should be running in parallel to the same exact CPUs.
   if(NOT "${SHADOW_TEST_ARGS}" MATCHES ".*--use-cpu-pinning.*")
      list(APPEND SHADOW_TEST_ARGS "--use-cpu-pinning" "false")
   endif()

   # If strace logging is not explicitly set, we enable it
   if(NOT "${SHADOW_TEST_ARGS}" MATCHES ".*--strace-logging-mode.*")
      list(APPEND SHADOW_TEST_ARGS "--strace-logging-mode" "standard")
   endif()

   string(REPLACE ";" " " SHADOW_TEST_ARGS "${SHADOW_TEST_ARGS}")

   set(SHADOW_TEST_NAME ${SHADOW_TEST_BASENAME}-shadow)

   if(DEFINED SHADOW_TEST_POST_CMD)
      set(POST_CMD "cd ${SHADOW_TEST_NAME}.data && ${SHADOW_TEST_POST_CMD}")
   else()
      set(POST_CMD "true")
   endif()

   if(SHADOW_TEST_CHECK_RETVAL)
      # Validate that all processes exited with code 0.  Processes that don't
      # exit have an empty exitcode file, which will cause this check to
      # fail.
      # We *don't* fail in the case that nothing matched the glob, which is
      # why we need to explicitly check whether the file exists.
      set(CHECK_RETVAL_CMD "
            for f in ${SHADOW_TEST_NAME}.data/hosts/*/*.exitcode
            do
            # Check if glob actually matched
            if [ ! -f $f ]\; then continue\; fi
            # Check if the file contains 0
            if [ \"`cat $f`\" != 0 ]
            then
               echo FAIL: $f isn\\'t 0
               exit 1
            fi
         done")
   else()
      set(CHECK_RETVAL_CMD "true")
   endif()

   set(SHADOW_TEST_COMMAND sh -c "\
      rm -rf ${SHADOW_TEST_NAME}.data \
      && ${INVERT_ERROR_CODE} ${CMAKE_BINARY_DIR}/src/main/shadow \
      --data-directory=${SHADOW_TEST_NAME}.data \
      --log-level=${SHADOW_TEST_LOGLEVEL} \
      ${SHADOW_TEST_ARGS} \
      ${SHADOW_TEST_SHADOW_CONFIG} \
      && (${CHECK_RETVAL_CMD}) \
      && (${POST_CMD}) \
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

   # check that we didn't leak any reference-counted objects
   set(FAIL_REGEX "Memory leak detected")

   set_property(TEST ${SHADOW_TEST_NAME} PROPERTY ENVIRONMENT "RUST_BACKTRACE=1;G_DEBUG=fatal-criticals")

   set_property(TEST ${SHADOW_TEST_NAME} PROPERTY FAIL_REGULAR_EXPRESSION ${FAIL_REGEX})

   if(DEFINED SHADOW_TEST_PROPERTIES)
      set_tests_properties(${SHADOW_TEST_NAME} PROPERTIES ${SHADOW_TEST_PROPERTIES})
   endif()

   set_property(TEST ${SHADOW_TEST_NAME} APPEND PROPERTY LABELS shadow)
endmacro()
## === end test helper macros ===

add_subdirectory(bindc)
add_subdirectory(clone)
add_subdirectory(compressed-graph)
add_subdirectory(config)
add_subdirectory(cpp)
add_subdirectory(determinism)
add_subdirectory(dup)
add_subdirectory(environment)
add_subdirectory(epoll)
add_subdirectory(eventfd)
add_subdirectory(examples)
add_subdirectory(exit)
add_subdirectory(file)
add_subdirectory(futex)
add_subdirectory(golang)
add_subdirectory(ifaddrs)
add_subdirectory(itimer)
add_subdirectory(memory)
add_subdirectory(phold)
add_subdirectory(pipe)
add_subdirectory(poll)
add_subdirectory(random)
add_subdirectory(regression)
add_subdirectory(resolver)
add_subdirectory(sched_affinity)
add_subdirectory(select)
add_subdirectory(signal)
add_subdirectory(sleep)
add_subdirectory(sockbuf)
add_subdirectory(socket)
add_subdirectory(stdio)
add_subdirectory(sysinfo)
add_subdirectory(tcp)
add_subdirectory(tgen)
add_subdirectory(threads)
add_subdirectory(timerfd)
add_subdirectory(tor)
add_subdirectory(udp)
add_subdirectory(unistd)
