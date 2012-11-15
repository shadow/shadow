## ping plug-in

set(ping_sources
    shd-ping-service.c
    shd-ping.c 
    shd-ping-server.c 
    shd-ping-client.c 
)

## service target to allow ping service to be used by any plugin
add_library(shadow-service-ping STATIC ${ping_sources})
target_link_libraries(shadow-service-ping ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## executable that can run outside of shadow
add_executable(shadow-ping shd-ping-main.c ${ping_sources})
target_link_libraries(shadow-ping ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-ping DESTINATION bin)

## build bitcode - other plugins may use the service bitcode target
add_bitcode(shadow-service-ping-bitcode ${ping_sources})
add_bitcode(shadow-plugin-ping-bitcode shd-ping-plugin.c)

## create and install a shared library that can plug into shadow
add_plugin(shadow-plugin-ping shadow-plugin-ping-bitcode shadow-service-ping-bitcode)
target_link_libraries(shadow-plugin-ping ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-ping DESTINATION plugins)

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-ping-bitcode EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)
