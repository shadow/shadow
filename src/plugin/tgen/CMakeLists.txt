## tgen: an all-purpose traffic generation plug-in for Shadow
set(tgen_sources
    shd-tgen-action.c
    shd-tgen-driver.c
    shd-tgen-graph.c
    shd-tgen-io.c
    shd-tgen-peer.c
    shd-tgen-pool.c
    shd-tgen-server.c
    shd-tgen-timer.c
    shd-tgen-transfer.c
    shd-tgen-transport.c
)

find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## executable that can run outside of shadow
add_executable(shadow-tgen shd-tgen-main.c ${tgen_sources})
target_link_libraries(shadow-tgen ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-tgen DESTINATION bin)

## build bitcode
add_bitcode(shadow-plugin-tgen-bitcode shd-tgen-plugin.c ${tgen_sources})

## create and install the shared library that plugs into shadow
add_plugin(shadow-plugin-tgen shadow-plugin-tgen-bitcode)
target_link_libraries(shadow-plugin-tgen ${RT_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-tgen DESTINATION plugins)
