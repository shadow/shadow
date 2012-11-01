## example echo server/client plug-in

## executable that can run outside of shadow
add_executable(shadow-echo shd-echo-main.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
target_link_libraries(shadow-echo ${GLIB_LIBRARIES})
install(TARGETS shadow-echo DESTINATION bin)

## create and install a shared library that can plug into shadow
add_bitcode(shadow-plugin-echo-bitcode 
    shd-echo-plugin.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
add_plugin(shadow-plugin-echo shadow-plugin-echo-bitcode)
target_link_libraries(shadow-plugin-echo ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-echo DESTINATION plugins)
