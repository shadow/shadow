## example TCP/UDP ping server and client
## create and install a shared library that can plug into shadow
add_library(shadow-plugin-pingpong MODULE shd-pingpong.c shd-pingpong-client.c shd-pingpong-server.c)
install(TARGETS shadow-plugin-pingpong DESTINATION lib)