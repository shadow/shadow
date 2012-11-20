set(torcontrol-modules
	shd-torcontrol-circuitbuild.c
    shd-torcontrol-statistics.c
)

## executable that can run outside of shadow
add_executable(shadow-torcontrol shd-torcontrol-main.c shd-torcontrol.c  ${torcontrol-modules})
target_link_libraries(shadow-torcontrol ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES})
install(TARGETS shadow-torcontrol DESTINATION bin)

## a stand-alone shared library that can plug into shadow
add_bitcode(shadow-plugin-torcontrol-bitcode 
    shd-torcontrol-plugin.c shd-torcontrol-main.c shd-torcontrol.c ${torcontrol-modules})
add_plugin(shadow-plugin-torcontrol shadow-plugin-torcontrol-bitcode)
target_link_libraries(shadow-plugin-torcontrol ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES})
install(TARGETS shadow-plugin-torcontrol DESTINATION plugins)

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)
