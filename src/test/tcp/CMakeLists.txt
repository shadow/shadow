include_directories(${GLIB_INCLUDE_DIRS})
link_libraries(${GLIB_LIBRARIES})
add_executable(test-tcp test_tcp.c)

foreach(BlockingMode blocking nonblocking-poll nonblocking-select nonblocking-epoll iov)
    add_linux_tests(
        BASENAME tcp-${BlockingMode}-loopback
        COMMAND ${CMAKE_SOURCE_DIR}/src/test/launch_with_unused_ports.py ../shadow-test-launcher test-tcp ${BlockingMode}
            server localhost @PORT@ : test-tcp ${BlockingMode} client localhost @PORT@
    )
    foreach(Network loopback lossless lossy)
        # the iov test isn't written in a way that supports lossy networks (it
        # assumes that it can read all bytes at once, and doesn't have the
        # logic to wait for the rest of the bytes after one readv call)
        if("${BlockingMode}" STREQUAL iov AND "${Network}" STREQUAL lossy)
            # TODO: support lossy iov tests
            continue()
        endif()
        add_shadow_tests(BASENAME tcp-${BlockingMode}-${Network})
    endforeach()
endforeach()
