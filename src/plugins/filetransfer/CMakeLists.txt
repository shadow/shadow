## HTTP file server and client for transfering files through shadow
include_directories(${CMAKE_SOURCE_DIR}/src/utility)

set(filetransfer_sources
    shd-filetransfer.c 
    shd-fileserver.c 
    shd-service-filegetter.c 
    shd-filegetter.c
    ../../utility/shd-cdf.c
)

## service target to allow filetransfer service to be used by any plugin
add_library(shadow-service-filetransfer STATIC ${filetransfer_sources})
add_dependencies(shadow-service-filetransfer shadow-util)
target_link_libraries(shadow-service-filetransfer ${RT_LIBRARIES})

## executable that can run outside of shadow
add_executable(shadow-filetransfer shd-filetransfer-main.c)
target_link_libraries(shadow-filetransfer shadow-service-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-filetransfer DESTINATION bin)

## build bitcode - other plugins may use the service bitcode target
add_bitcode(shadow-service-filetransfer-bitcode ${filetransfer_sources})
add_bitcode(shadow-plugin-filetransfer-bitcode shd-filetransfer-plugin.c)

## create and install a shared library that can plug into shadow
add_plugin(shadow-plugin-filetransfer shadow-plugin-filetransfer-bitcode shadow-service-filetransfer-bitcode)
target_link_libraries(shadow-plugin-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-filetransfer DESTINATION plugins)

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-filetransfer shadow-service-filetransfer-bitcode EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)