## the plug-ins need to include shd-library.h
include_directories(${SHADOW_SOURCE_DIR}/library)
install(FILES shd-library.h DESTINATION include/)
install(FILES ${SHADOW_SOURCE_DIR}/utility/shd-cdf.h DESTINATION include/)

## plug-ins should install here
set(SHADOW_PLUGIN_DIR $ENV{HOME}/.shadow/plugins)

## build each plug-in that was not disabled in setup.py

if(NOT BUILD_BROWSER STREQUAL OFF)
    add_subdirectory(browser)
endif(NOT BUILD_BROWSER STREQUAL OFF)

if(NOT BUILD_ECHO STREQUAL OFF)
    add_subdirectory(echo)
endif(NOT BUILD_ECHO STREQUAL OFF)

if(NOT BUILD_FILETRANSFER STREQUAL OFF)
    add_subdirectory(filetransfer)
endif(NOT BUILD_FILETRANSFER STREQUAL OFF)

if(NOT BUILD_SCALLION STREQUAL OFF)
    add_subdirectory(scallion)
endif(NOT BUILD_SCALLION STREQUAL OFF)

if(NOT BUILD_TORRENT STREQUAL OFF)
    add_subdirectory(torrent)
endif(NOT BUILD_TORRENT STREQUAL OFF)
