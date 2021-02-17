include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-tcp test_tcp.c)

# FIXME: enable other blocking modes
foreach(BlockingMode blocking nonblocking-epoll) # nonblocking-poll nonblocking-epoll nonblocking-select iov)
    add_test(
        NAME tcp-${BlockingMode}-loopback
        COMMAND ${CMAKE_SOURCE_DIR}/src/test/launch_with_unused_ports.py ../shadow-test-launcher test-tcp ${BlockingMode}
            server localhost @PORT@ : test-tcp ${BlockingMode} client localhost @PORT@
    )
    list(APPEND TestNames tcp-${BlockingMode}-loopback)

    foreach(Network loopback lossless lossy)
        # FIXME: enable preload
        foreach(InterposeMethod ptrace preload ptrace-only)
            add_test(
                NAME tcp-${BlockingMode}-${Network}-shadow-${InterposeMethod}
                COMMAND sh -c "\
                ${yaml2xml} ${CMAKE_CURRENT_SOURCE_DIR}/tcp-${BlockingMode}-${Network}.test.shadow.config.yaml --output - \
                | ${CMAKE_BINARY_DIR}/src/main/shadow --interpose-method=${InterposeMethod} -l debug -d ${BlockingMode}-${Network}.shadow-${InterposeMethod}.data - \
                "
            )
            list(APPEND TestNames tcp-${BlockingMode}-${Network}-shadow-${InterposeMethod})
        endforeach()
    endforeach()
endforeach()

set_property(TEST ${TestNames} PROPERTY FAIL_REGULAR_EXPRESSION "main error code '.*' for process")
set_property(TEST ${TestNames} PROPERTY RUN_SERIAL TRUE)
