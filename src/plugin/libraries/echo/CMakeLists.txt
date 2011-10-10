## example TCP echo server and client
## create and install a shared library that can plug into shadow
add_library(shadow-plugin-echo MODULE shd-echo.c shd-echo-client.c shd-echo-server.c)
install(TARGETS shadow-plugin-echo DESTINATION lib)
