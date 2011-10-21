## the plug-ins need to include shadowlib.h
include_directories(${SHADOW_SOURCE_DIR}/plugin/libraries)
install(FILES shadowlib.h DESTINATION include/shadow)

## build each plug-in
add_subdirectory(echo)
add_subdirectory(pingpong)
add_subdirectory(filetransfer)