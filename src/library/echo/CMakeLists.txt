## example TCP echo server and client
## create and install a shared library that can plug into shadow
add_library(shadow-plugin-echo MODULE shd-echo-plugin.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
install(TARGETS shadow-plugin-echo DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-echo shd-echo-main.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
target_link_libraries(shadow-echo ${GLIB_LIBRARIES})
install(TARGETS shadow-echo DESTINATION bin)
