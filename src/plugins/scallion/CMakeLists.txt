## check for dependencies, using built-in cmake files to help us find the libs
## these are in /usr/share/cmake/Modules/Findxxx.cmake
find_package(ZLIB REQUIRED)

## other libs dont have a Findxxx.cmake, so we check on our own (./cmake)
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(OPENSSL REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(GLIB REQUIRED)
find_package(TOR REQUIRED)
find_package(TIDY REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${EVENT2_INCLUDES} ${OPENSSL_INCLUDES} ${TOR_INCLUDES} ${SHADOW_INCLUDES} ${TIDY_INCLUDES})
include_directories(${CMAKE_SOURCE_DIR}/src/utility)
include_directories(${CMAKE_SOURCE_DIR}/src/library/browser)
include_directories(${CMAKE_SOURCE_DIR}/src/library/filetransfer)
include_directories(${CMAKE_SOURCE_DIR}/src/library/torrent)

## import shadow libs
#INCLUDE(${CMAKE_INSTALL_PREFIX}/share/shadow/shadow-externals.cmake)

## create scallion plug-in library for shadow
add_library(shadow-plugin-scallion MODULE scallion-plugin.c scallion-registration.c scallion-tor.c)
add_dependencies(shadow-plugin-scallion shadow-service-filetransfer shadow-service-torrent shadow-service-browser)

## link with the required dependencies
#-lz -lm -levent -lssl -lcrypto -ldl
target_link_libraries(shadow-plugin-scallion shadow-service-browser shadow-service-filetransfer shadow-service-torrent
    ${TOR_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} 
    ${TIDY_LIBRARIES} ${ZLIB_LIBRARIES} ${M_LIBRARIES} ${RT_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-plugin-scallion DESTINATION $ENV{HOME}/.shadow/plugins)
set_target_properties(shadow-plugin-scallion PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE)

## preload library for intercepting functions
add_library(shadow-preload-scallion SHARED scallion-preload.c)
target_link_libraries(shadow-preload-scallion ${GLIB_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-preload-scallion DESTINATION lib)

## install scallion script
install(PROGRAMS scallion DESTINATION bin)

install(FILES ${SCALLION_TORPATH}/src/config/geoip DESTINATION share/)
#install(DIRECTORY resource/ DESTINATION share PATTERN "config/*")