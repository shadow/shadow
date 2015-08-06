## tgen: an all-purpose traffic generation plug-in for Shadow
project(shadow-plugin-tgen)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

option(SKIP_SHADOW "only build the tgen tool for running outside of shadow (default: OFF)" OFF)

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

if(SKIP_SHADOW STREQUAL ON)
    message(STATUS "building tgen as external tool only, skipping Shadow plugin")
    message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")
endif()

add_definitions(-D_GNU_SOURCE)

find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

if(SKIP_SHADOW STREQUAL ON)
    include_directories(AFTER ../)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")

    add_executable(tgen ${tgen_sources} shd-tgen-global-lock.c)
    target_link_libraries(tgen ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
    install(TARGETS tgen DESTINATION bin)

    return()
endif()

## skip checks if built in shadow source dir
if(NOT TARGET LLVMHoistGlobals)
    message(FATAL_ERROR "shadow-plugin-tgen must currently be built in shadow source tree")
#
#    ## fill in LLVM variables since we did not inherit from shadow cmake file
#    include(LLVMTools)
#    add_custom_target(LLVMHoistGlobals)
#    set(LLVMHoistGlobalsPATH "${SHADOW_ROOT}/lib/LLVMHoistGlobals.so")
#
#    ## make sure our LLVM plugin exists
#    if(EXISTS "${LLVMHoistGlobalsPATH}")
#        message(STATUS "LLVMHoistGlobalsPATH = ${LLVMHoistGlobalsPATH}")
#    else()
#        message(FATAL_ERROR "LLVMHoistGlobalsPATH path does not exist: '${LLVMHoistGlobalsPATH}'")
#    endif()
endif()

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

## library to manage global locking
add_library(shadow-tgen-global-lock SHARED shd-tgen-global-lock.c)
target_link_libraries(shadow-tgen-global-lock ${GLIB_LIBRARIES})
install(TARGETS shadow-tgen-global-lock DESTINATION lib)

## the main tgen dynamic executable, can run inside or outside of shadow
#add_executable(tgen ${tgen_sources})
#set_target_properties(tgen PROPERTIES 
#        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
#        INSTALL_RPATH_USE_LINK_PATH TRUE 
#        LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed")
#add_dependencies(tgen shadow-tgen-global-lock)
#target_link_libraries(tgen shadow-tgen-global-lock ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
#install(TARGETS tgen DESTINATION bin)

## create and install the dynamic executable that can plug into shadow
add_shadow_plugin(shadow-plugin-tgen ${tgen_sources})
add_dependencies(shadow-plugin-tgen shadow-tgen-global-lock)
target_link_libraries(shadow-plugin-tgen shadow-tgen-global-lock ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-tgen DESTINATION plugins)
