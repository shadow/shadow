## HTTP file server and client for transfering files through shadow
include_directories(${CMAKE_SOURCE_DIR}/src/utility)

set(filetransfer_sources
    shd-filetransfer.c 
    shd-fileserver.c 
    shd-service-filegetter.c 
    shd-filegetter.c
    ../../utility/shd-cdf.c
)

## service bitcode target to allow filetransfer bitcode target to be used by any plugin
#add_library(shadow-service-filetransfer STATIC ${filetransfer_sources})
#add_dependencies(shadow-service-filetransfer shadow-util)
#target_link_libraries(shadow-service-filetransfer ${RT_LIBRARIES})

## a stand-alone shared library that can plug into shadow
#add_library(shadow-plugin-filetransfer MODULE shd-filetransfer-plugin.c)
#target_link_libraries(shadow-plugin-filetransfer shadow-service-filetransfer)
#install(TARGETS shadow-plugin-filetransfer DESTINATION plugins)

## executable that can run outside of shadow
add_executable(shadow-filetransfer shd-filetransfer-main.c ${filetransfer_sources})
target_link_libraries(shadow-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-filetransfer DESTINATION bin)

## build bitcode - other plugins may use the service bitcode target
add_bitcode(shadow-service-filetransfer-bitcode ${filetransfer_sources})
add_bitcode(shadow-plugin-filetransfer-bitcode shd-filetransfer-plugin.c)

## create and install a shared library that can plug into shadow
add_plugin(shadow-plugin-filetransfer shadow-plugin-filetransfer-bitcode shadow-service-filetransfer-bitcode)
target_link_libraries(shadow-plugin-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-filetransfer DESTINATION plugins)