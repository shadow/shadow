## P2P torrent transfers plug-in

set(torrent_sources
    shd-torrent-service.c
    shd-torrent.c 
    shd-torrent-server.c 
    shd-torrent-client.c 
    shd-torrent-authority.c
)

## executable that can run outside of shadow
add_executable(shadow-torrent shd-torrent-main.c ${torrent_sources})
target_link_libraries(shadow-torrent ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-torrent DESTINATION bin)

## build bitcode - other plugins may use the service bitcode target
add_bitcode(shadow-plugin-torrent-bitcode shd-torrent-plugin.c ${torrent_sources})

## create and install a shared library that can plug into shadow
add_plugin(shadow-plugin-torrent shadow-plugin-torrent-bitcode)
target_link_libraries(shadow-plugin-torrent ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-torrent DESTINATION plugins)
