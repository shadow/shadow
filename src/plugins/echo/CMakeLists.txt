## example echo server/client plug-in

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
add_dependencies(shadow-plugin-echo LLVMHoistGlobals)
target_link_libraries(shadow-plugin-echo ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-echo DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
#add_executable(shadow-echo shd-echo-main.c shd-echo-tcp.c shd-echo-udp.c shd-echo-pipe.c)
#target_link_libraries(shadow-echo ${GLIB_LIBRARIES})
#install(TARGETS shadow-echo DESTINATION bin)
