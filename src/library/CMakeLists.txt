## the plug-ins need to include shd-library.h
include_directories(${SHADOW_SOURCE_DIR}/library)
install(FILES shd-library.h DESTINATION include/shadow)
install(FILES ${SHADOW_SOURCE_DIR}/utility/shd-cdf.h DESTINATION include/shadow)

## plug-ins should install here
set(SHADOW_PLUGIN_DIR $ENV{HOME}/.shadow/plugins)

## build each plug-in
add_subdirectory(echo)
#add_subdirectory(filetransfer)