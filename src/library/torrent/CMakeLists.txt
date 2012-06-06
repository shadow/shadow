add_definitions(-D_GNU_SOURCE)

## service library to allow torrent to be used by any plugin
add_library(shadow-service-torrent STATIC shd-torrent-service.c shd-torrent.c shd-torrent-server.c shd-torrent-client.c shd-torrent-authority.c)
target_link_libraries(shadow-service-torrent ${RT_LIBRARIES})
install(TARGETS shadow-service-torrent EXPORT shadow-externals DESTINATION lib)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-torrent MODULE shd-torrent-plugin.c shd-torrent-main.c shd-torrent.c shd-torrent-server.c shd-torrent-client.c shd-torrent-authority.c)
#target_link_libraries(shadow-plugin-torrent shadow-service-torrent)
install(TARGETS shadow-plugin-torrent DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-torrent shd-torrent-main.c shd-torrent.c shd-torrent-server.c shd-torrent-client.c shd-torrent-authority.c)
target_link_libraries(shadow-torrent ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-torrent DESTINATION bin)

install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")