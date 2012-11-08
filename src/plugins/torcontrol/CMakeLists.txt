set(tor-control-modules
	shd-tor-control-circuitbuild.c
)

## executable that can run outside of shadow
add_executable(shadow-tor-control shd-tor-control-main.c shd-tor-control.c  ${tor-control-modules})
target_link_libraries(shadow-tor-control ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES})
install(TARGETS shadow-tor-control DESTINATION bin)

## a stand-alone shared library that can plug into shadow
add_bitcode(shadow-plugin-tor-control-bitcode 
    shd-tor-control-plugin.c shd-tor-control-main.c shd-tor-control.c ${tor-control-modules})
add_plugin(shadow-plugin-tor-control shadow-plugin-tor-control-bitcode)
target_link_libraries(shadow-plugin-tor-control ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES})
install(TARGETS shadow-plugin-tor-control DESTINATION plugins)

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)
