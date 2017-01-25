## tgen: an all-purpose traffic generation plug-in for Shadow
project(shadow-plugin-tgen)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

set(tgen_sources
    shd-tgen-action.c
    shd-tgen-driver.c
    shd-tgen-graph.c
    shd-tgen-io.c
    shd-tgen-main.c
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

include_directories(AFTER ../ ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -std=gnu11 -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

add_definitions(-D_GNU_SOURCE)

## the main tgen dynamic executable, can run inside or outside of shadow
## this only works because none of tgen's internal symbols need to be interposed!
add_executable(tgen ${tgen_sources})
set_target_properties(tgen PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed")
target_link_libraries(tgen ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS tgen DESTINATION bin)

## the following is not strictly needed, because the above exe can also run inside of shadow
option(SKIP_SHADOW "only build the tgen tool for running outside of shadow (default: OFF)" OFF)
if(SKIP_SHADOW STREQUAL OFF)
    add_shadow_plugin(shadow-plugin-tgen ${tgen_sources})
    target_link_libraries(shadow-plugin-tgen ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
    install(TARGETS shadow-plugin-tgen DESTINATION lib)
endif()
