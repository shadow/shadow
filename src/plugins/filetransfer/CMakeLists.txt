## HTTP file server and client for transfering files through shadow
include_directories(${CMAKE_SOURCE_DIR}/src/utility)

set(filetransfer_sources
    shd-filetransfer.c 
    shd-fileserver.c 
    shd-service-filegetter.c 
    shd-filegetter.c
    ${CMAKE_SOURCE_DIR}/src/utility/shd-cdf.c
)

## service library to allow filetransfer to be used by any plugin
add_library(shadow-service-filetransfer STATIC ${filetransfer_sources})
add_dependencies(shadow-service-filetransfer shadow-util)
target_link_libraries(shadow-service-filetransfer ${RT_LIBRARIES})

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-filetransfer EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-filetransfer MODULE shd-filetransfer-plugin.c)
target_link_libraries(shadow-plugin-filetransfer shadow-service-filetransfer)
install(TARGETS shadow-plugin-filetransfer DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-filetransfer shd-filetransfer-main.c)
target_link_libraries(shadow-filetransfer shadow-service-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-filetransfer DESTINATION bin)
