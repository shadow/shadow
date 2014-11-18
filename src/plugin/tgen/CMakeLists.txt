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

## library to manage global locking
add_library(shadow-tgen-global-lock SHARED shd-tgen-global-lock.c)
target_link_libraries(shadow-tgen-global-lock ${GLIB_LIBRARIES})
install(TARGETS shadow-tgen-global-lock DESTINATION lib)

## executable that can run outside of shadow
add_executable(shadow-tgen shd-tgen-main.c ${tgen_sources})
add_dependencies(shadow-tgen shadow-tgen-global-lock)
target_link_libraries(shadow-tgen shadow-tgen-global-lock ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-tgen DESTINATION bin)

## build bitcode
add_bitcode(shadow-plugin-tgen-bitcode shd-tgen-plugin.c ${tgen_sources})

## create and install the shared library that plugs into shadow
add_plugin(shadow-plugin-tgen shadow-plugin-tgen-bitcode)
add_dependencies(shadow-plugin-tgen shadow-tgen-global-lock)
target_link_libraries(shadow-plugin-tgen shadow-tgen-global-lock ${RT_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-tgen DESTINATION plugins)
