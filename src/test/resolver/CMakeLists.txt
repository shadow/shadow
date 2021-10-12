find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDES})

## create and install an executable that can run outside of shadow
add_executable(test-getaddrinfo test_getaddrinfo.c)

## if the test needs any libraries, link them here
target_link_libraries(test-getaddrinfo ${GLIB_LIBRARIES})

## register the tests
add_linux_tests(BASENAME getaddrinfo COMMAND test-getaddrinfo)
add_shadow_tests(BASENAME getaddrinfo)
