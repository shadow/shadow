## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-rawsock shd-test-raw-sock.c)

## create and install an executable that can run outside of shadow
add_executable(test-rawsock shd-test-raw-sock.c)

## register the tests

## Creating raw socket 
## these also test "localhost" instead of "127.0.0.1" in the loopback tests
add_test(
    NAME raw-socket
    COMMAND shadow-test-launcher test-rawsock  server : test-rawsock  client localhost
)
add_test(
    NAME rawsock-shadow
    COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d rawsock.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/raw-socket.test.shadow.config.xml
)
