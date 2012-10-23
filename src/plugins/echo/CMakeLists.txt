## example echo server/client plug-in

#SET(CMAKE_C_COMPILE_OBJECT
#    "<CMAKE_C_COMPILER> <FLAGS> -c <SOURCE> -o <SOURCE>.bc"
#)

## when linking: link the bitcode, hoist globals, then create the plug-in module
## this is run from: shadow/build/shadow/src/plugins/echo
set(CMAKE_C_CREATE_SHARED_MODULE 
    "llvm-link -o <TARGET>.bc <OBJECTS>"
    "opt -load=../../hoist/LLVMHoistGlobals.so -hoist-globals <TARGET>.bc -o <TARGET>.hoisted.bc"
    "<CMAKE_C_COMPILER> -shared -fPIC <LINK_FLAGS> <TARGET>.hoisted.bc -o <TARGET> <LINK_LIBRARIES>"
)

## lets get llvm bitcode as output when compiling
add_definitions(-emit-llvm)

get_property(hoistpath TARGET LLVMHoistGlobals PROPERTY LOCATION)
message(STATUS "hoistpath = ${hoistpath}")

## create and install a shared library that can plug into shadow
add_library(shadow-plugin-echo MODULE shd-echo-plugin.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
target_link_libraries(shadow-plugin-echo ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-echo DESTINATION ${SHADOW_PLUGIN_DIR})

#add_library(shadow-plugin-echo MODULE shd-echo-plugin.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)

## executable that can run outside of shadow
#add_executable(shadow-echo shd-echo-main.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
#target_link_libraries(shadow-echo ${GLIB_LIBRARIES})
#install(TARGETS shadow-echo DESTINATION bin)

## llvm-link
#add_custom_command(OUTPUT echo.bc
#                   COMMAND /home/rob/.local/bin/llvm-link
#                   ARGS shd-echo-plugin.bc shd-echo-tcp.bc shd-echo-udp.bc shd-echo-pipe.bc "-o" "echo.bc"
#                   DEPENDS shadow-plugin-echo-bitcodes
#                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#                   COMMENT "Linking bitcode")
## opt, hoist                   
#add_custom_command(OUTPUT echo.hoisted.bc
#                   COMMAND opt
#                   ARGS -load=$<TARGET_FILE:LLVMHoistGlobals> "echo.bc" "-o" "echo.hoisted.bc"
#                   DEPENDS "echo.bc"
#                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#                   COMMENT "Hoisting variables")
## assembly
#add_custom_command(OUTPUT echo.hoisted.s
#                   COMMAND llc
#                   ARGS "echo.hoisted.bc" "-o" "echo.hoisted.s"
#                   DEPENDS "echo.hoisted.bc"
#                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#                   COMMENT "compiling assembly")
## the actual .so
#add_library(shadow-plugin-echo MODULE echo.hoisted.s)
#target_link_libraries(shadow-plugin-echo ${GLIB_LIBRARIES})