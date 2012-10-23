## P2P torrent transfers plug-in

## service library to allow torrent to be used by any plugin
add_library(shadow-service-torrent STATIC shd-torrent-service.c shd-torrent.c shd-torrent-server.c shd-torrent-client.c shd-torrent-authority.c)
target_link_libraries(shadow-service-torrent ${RT_LIBRARIES})

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-torrent EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-torrent MODULE shd-torrent-plugin.c)
target_link_libraries(shadow-plugin-torrent shadow-service-torrent)
install(TARGETS shadow-plugin-torrent DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-torrent shd-torrent-main.c shd-torrent.c shd-torrent-server.c shd-torrent-client.c shd-torrent-authority.c)
target_link_libraries(shadow-torrent ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-torrent DESTINATION bin)
