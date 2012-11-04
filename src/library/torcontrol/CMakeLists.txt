add_definitions(-D_GNU_SOURCE)

set(tor-control-modules
	shd-tor-control-circuitbuild.c
)

## service library to allow tor-control to be used by any plugin
#add_library(shadow-service-tor-control STATIC shd-tor-control-service.c shd-tor-control-server.c shd-tor-control-client.c shd-tor-control.c ${tor-control-modules})
#target_link_libraries(shadow-service-tor-control ${RT_LIBRARIES})
#install(TARGETS shadow-service-tor-control EXPORT shadow-externals DESTINATION lib)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-tor-control MODULE shd-tor-control-plugin.c shd-tor-control-main.c shd-tor-control.c ${tor-control-modules})
#target_link_libraries(shadow-plugin-tor-control shadow-service-tor-control)
install(TARGETS shadow-plugin-tor-control DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-tor-control shd-tor-control-main.c shd-tor-control.c  ${tor-control-modules})
target_link_libraries(shadow-tor-control ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES})
install(TARGETS shadow-tor-control DESTINATION bin)

install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")