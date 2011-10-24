## the plug-ins need to include shd-library.h
include_directories(${SHADOW_SOURCE_DIR}/plugin/libraries)
install(FILES shd-library.h DESTINATION include/shadow)

## build each plug-in
add_subdirectory(echo)
add_subdirectory(pingpong)
add_subdirectory(filetransfer)