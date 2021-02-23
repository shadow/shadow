include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})
add_executable(test-tcp test_tcp.c)

# FIXME: enable other blocking modes
foreach(BlockingMode blocking nonblocking-poll nonblocking-epoll) # TODO nonblocking-select iov
    add_linux_tests(
        BASENAME tcp-${BlockingMode}-loopback
        COMMAND ${CMAKE_SOURCE_DIR}/src/test/launch_with_unused_ports.py ../shadow-test-launcher test-tcp ${BlockingMode}
            server localhost @PORT@ : test-tcp ${BlockingMode} client localhost @PORT@
    )
    foreach(Network loopback lossless lossy)
        add_shadow_tests(BASENAME tcp-${BlockingMode}-${Network} METHODS ptrace-only ptrace preload)
    endforeach()
endforeach()