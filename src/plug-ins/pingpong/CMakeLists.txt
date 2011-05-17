## create the simple_udp library, shared lib that can plug into DVN
add_library(shadow-plugin-pingpongudp MODULE pingpong_udp.c pingpong_lib.c)

## simple_udp uses the snri lib to communicate with DVN
target_link_libraries(shadow-plugin-pingpongudp shadow-snri)

## create the simple_tcp library, shared lib that can plug into DVN
add_library(shadow-plugin-pingpongtcp MODULE pingpong_tcp.c pingpong_lib.c)

## simple_tcp uses the snri lib to communicate with DVN
target_link_libraries(shadow-plugin-pingpongtcp shadow-snri)

add_dependencies(shadow-plugin-pingpongtcp shadow-snri)
add_dependencies(shadow-plugin-pingpongudp shadow-snri)

install(TARGETS shadow-plugin-pingpongtcp shadow-plugin-pingpongudp DESTINATION lib)
install(FILES pingpong-tcp.dsim pingpong-udp.dsim DESTINATION share/shadow)
