## HTTP file server and client for transfering files through shadow
add_definitions(-D_GNU_SOURCE)
include_directories(${SHADOW_SOURCE_DIR}/utility)

set(filetransfer_sources
    shd-filetransfer.c 
    shd-fileserver.c 
    shd-service-filegetter.c 
    shd-filegetter.c
    ${SHADOW_SOURCE_DIR}/utility/shd-cdf.c
)

## service library to allow filetransfer to be used by any plugin
add_library(shadow-service-filetransfer STATIC ${filetransfer_sources})
add_dependencies(shadow-service-filetransfer shadow-util)
target_link_libraries(shadow-service-filetransfer ${RT_LIBRARIES})
install(TARGETS shadow-service-filetransfer EXPORT shadow-externals DESTINATION lib)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-filetransfer MODULE shd-filetransfer-plugin.c)
target_link_libraries(shadow-plugin-filetransfer shadow-service-filetransfer)
install(TARGETS shadow-plugin-filetransfer DESTINATION ${SHADOW_PLUGIN_DIR})

## executables that can run outside of shadow
#add_executable(shadow-fileserver shd-fileserver-main.c shd-fileserver.c)
#target_link_libraries(shadow-fileserver ${GLIB_LIBRARIES})
#add_executable(shadow-filegetter shd-filegetter-main.c shd-service-filegetter.c shd-filegetter.c ${SHADOW_SOURCE_DIR}/utility/shd-cdf.c ${SHADOW_SOURCE_DIR}/utility/rand.c)
#target_link_libraries(shadow-filegetter ${RT_LIBRARIES} ${GLIB_LIBRARIES})
#install(TARGETS shadow-fileserver shadow-filegetter DESTINATION bin)

install(DIRECTORY ./ DESTINATION include/shadow FILES_MATCHING PATTERN "*.h")
