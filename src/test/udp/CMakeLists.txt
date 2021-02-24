include_directories(${GLIB_INCLUDES})
link_libraries(${GLIB_LIBRARIES})

add_executable(test-udp test_udp.c)
add_linux_tests(BASENAME udp COMMAND sh -c "\
    rm -f udp-fifo && mkfifo udp-fifo && ../shadow-test-launcher test-udp client localhost:0 udp-fifo : test-udp server localhost:0 udp-fifo \
    "
)
add_shadow_tests(BASENAME udp METHODS hybrid ptrace preload)

add_executable(test-udp-uniprocess test_udp_uniprocess.c)
add_linux_tests(BASENAME udp-uniprocess COMMAND test-udp-uniprocess)
add_shadow_tests(BASENAME udp-uniprocess METHODS hybrid ptrace preload)