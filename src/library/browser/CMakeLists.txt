## example TCP echo server and client
## create and install a shared library that can plug into shadow
add_library(shadow-plugin-browser MODULE shd-browser-plugin.c)
install(TARGETS shadow-plugin-browser DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-browser shd-browser-main.c)
target_link_libraries(shadow-browser ${GLIB_LIBRARIES})
install(TARGETS shadow-browser DESTINATION bin)