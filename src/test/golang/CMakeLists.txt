include(ExternalProject)

# Fake target to build all golang tests
add_custom_target(golang_tests)

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
    add_dependencies(golang_tests ${AGTE_BASENAME})
endmacro()

add_golang_test_exe(BASENAME test_simple_http)
add_shadow_tests(
    BASENAME simple_http
    CHECK_RETVAL false
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

add_golang_test_exe(BASENAME test_goroutines)
add_linux_tests(
    BASENAME goroutines
    COMMAND ./test_goroutines
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)
add_shadow_tests(
    BASENAME goroutines
    # BUG: https://github.com/shadow/shadow/issues/1932
    SKIP_METHODS preload ptrace
    CONFIGURATIONS extra
    PROPERTIES
      LABELS golang)