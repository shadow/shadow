## create scallion plug-in library for shadow
add_library(shadow-plugin-scallion MODULE scallion-plugin.c scallion_registration.c vtor.c vtorflow.c)

## link with the required dependencies
#-lz -lm -levent -lssl -lcrypto -ldl
target_link_libraries(shadow-plugin-scallion ${SHADOW_LIBRARIES} ${TOR_LIBRARIES} 
    z ${M_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} ${RT_LIBRARIES} ${DL_LIBRARIES})

## preload library for intercepting functions
add_library(shadow-preload-scallion SHARED vtor_preload.c)
target_link_libraries(shadow-preload-scallion ${DL_LIBRARIES})

install(TARGETS shadow-preload-scallion DESTINATION lib)
install(TARGETS shadow-plugin-scallion DESTINATION $ENV{HOME}/.shadow/plugins)

## install scallion script
install(PROGRAMS scallion DESTINATION bin)
