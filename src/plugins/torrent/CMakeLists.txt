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
add_bitcode(shadow-service-torrent-bitcode ${torrent_sources})
add_bitcode(shadow-plugin-torrent-bitcode shd-torrent-plugin.c)

## create and install a shared library that can plug into shadow
add_plugin(shadow-plugin-torrent shadow-plugin-torrent-bitcode shadow-service-torrent-bitcode)
target_link_libraries(shadow-plugin-torrent ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-torrent DESTINATION plugins)

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-torrent-bitcode EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)
