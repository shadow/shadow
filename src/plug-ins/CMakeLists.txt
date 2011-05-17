## this project defines all modules that can be created for use with shadow
add_definitions(-fPIC)

## create the snri library
## its a static lib because it needs to be linked to a module and plugged into dhadow
add_library(shadow-snri STATIC snri.c)

## now recurse into plug-in dirs

## example TCP and UDP application modules and their dependency relations
add_subdirectory(pingpong)

## example TCP echo server and client
add_subdirectory(echo)

## http server and client
add_subdirectory(filetransfer)
