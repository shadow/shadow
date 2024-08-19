include(ExternalProject)

macro(add_golang_test_exe)
    cmake_parse_arguments(AGTE "" "BASENAME" "" ${ARGN})
    ExternalProject_Add(
        ${AGTE_BASENAME}
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}
        BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND ""
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        BUILD_COMMAND
          go
          build
          # Ensure LD_PRELOAD will work. Without this golang programs may or may
          # not be linked dynamically, depending on their dependencies.
          -linkshared
          -o ${CMAKE_CURRENT_BINARY_DIR}/${AGTE_BASENAME}
          ${CMAKE_CURRENT_SOURCE_DIR}/${AGTE_BASENAME}.go
    )
    set_property(TARGET ${AGTE_BASENAME} PROPERTY EXCLUDE_FROM_ALL true)
    add_dependencies(extra_tests ${AGTE_BASENAME})

    # Horrible hack to force golang targets to build serially.  Seeing if it
    # helps with https://github.com/shadow/shadow/issues/2341.  Using job pools
    # would be a nicer way of doing this, but is only supported for cmake's
    # ninja backend.
    # https://cmake.org/cmake/help/latest/prop_gbl/JOB_POOLS.html#prop_gbl:JOB_POOLS
    if (NOT "${PREV_GOLANG_TARGET}" STREQUAL "")
      add_dependencies(${AGTE_BASENAME} "${PREV_GOLANG_TARGET}")
    endif()
    set(PREV_GOLANG_TARGET "${AGTE_BASENAME}")
endmacro()

add_golang_test_exe(BASENAME test_simple_http)
add_shadow_tests(
    BASENAME simple_http
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)

add_golang_test_exe(BASENAME test_gc)
add_linux_tests(
    BASENAME gc
    COMMAND ./test_gc
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)
add_shadow_tests(
    BASENAME gc
    CONFIGURATIONS extra 
    PROPERTIES
      LABELS golang)

add_golang_test_exe(BASENAME test_intercept_golang_time)
add_shadow_tests(
    BASENAME intercept_golang_time
    CONFIGURATIONS extra 
    PROPERTIES
      LABELS golang)

add_golang_test_exe(BASENAME test_goroutines)
add_linux_tests(
    BASENAME goroutines
    COMMAND ./test_goroutines
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)
add_shadow_tests(
    BASENAME goroutines
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)

add_golang_test_exe(BASENAME test_go_preempt)
add_linux_tests(
    BASENAME go_preempt
    COMMAND ./test_go_preempt
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)
add_shadow_tests(
    BASENAME go_preempt
    CONFIGURATIONS extra
    # Doesn't work in ptrace-mode due to incomplete signal support.
    SKIP_METHODS ptrace
    # Trace level logging logs quite a bit in the "busy" loop here,
    # causing timeouts in debug builds.
    LOGLEVEL debug
    ARGS
      --strace-logging-mode off
    PROPERTIES
      # This test can take a bit longer in debug builds
      TIMEOUT 40
      LABELS golang)

add_golang_test_exe(BASENAME test_sigterm)
add_shadow_tests(
    BASENAME sigterm
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)
