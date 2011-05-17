## create each module library, shared libs that can plug into shadow
add_library(shadow-plugin-echoserver MODULE echo_server.c echo_lib.c)
add_library(shadow-plugin-echoclient MODULE echo_client.c echo_lib.c)
add_library(shadow-plugin-echoloopback MODULE echo_loopback.c echo_lib.c)

## the modules use the snri lib to communicate with shadow
target_link_libraries(shadow-plugin-echoserver shadow-snri)
target_link_libraries(shadow-plugin-echoclient shadow-snri)
target_link_libraries(shadow-plugin-echoloopback shadow-snri)

add_dependencies(shadow-plugin-echoserver shadow-snri)
add_dependencies(shadow-plugin-echoclient shadow-snri)

install(TARGETS shadow-plugin-echoserver shadow-plugin-echoclient shadow-plugin-echoloopback DESTINATION lib)
install(FILES echo-ethernet.dsim echo-loopback.dsim DESTINATION share/shadow)
