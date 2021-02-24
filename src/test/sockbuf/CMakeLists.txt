add_executable(test-sockbuf test_sockbuf.c ../test_common.c)
add_linux_tests(BASENAME sockbuf COMMAND test-sockbuf)
add_shadow_tests(BASENAME sockbuf METHODS hybrid ptrace preload)