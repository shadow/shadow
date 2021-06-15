find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test-getaddrinfo test_getaddrinfo.c)

## if the test needs any libraries, link them here
target_link_libraries(test-getaddrinfo ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-getaddrinfo COMMAND test-getaddrinfo)
add_test(NAME test-getaddrinfo-preload-libc COMMAND test-getaddrinfo)

SET_TESTS_PROPERTIES(test-getaddrinfo-preload-libc
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_BINARY_DIR}/src/shim/libshadow-shim.so")
