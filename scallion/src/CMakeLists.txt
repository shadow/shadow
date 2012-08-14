## create scallion plug-in library for shadow
add_library(shadow-plugin-scallion MODULE scallion-plugin.c scallion-registration.c scallion-tor.c)

## link with the required dependencies
#-lz -lm -levent -lssl -lcrypto -ldl
target_link_libraries(shadow-plugin-scallion ${SHADOW_LIBRARIES} ${TOR_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} ${TIDY_LIBRARIES}
    z ${M_LIBRARIES} ${RT_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-plugin-scallion DESTINATION $ENV{HOME}/.shadow/plugins)
set_target_properties(shadow-plugin-scallion PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE)

## preload library for intercepting functions
add_library(shadow-preload-scallion SHARED scallion-preload.c)
target_link_libraries(shadow-preload-scallion ${GLIB_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-preload-scallion DESTINATION lib)

## install scallion script
install(PROGRAMS scallion DESTINATION bin)
