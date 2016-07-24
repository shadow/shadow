## build the dynamic executable that plugs into shadow
add_shadow_exe(shadow-test-plugin-dynlink shd-test-dynlink-plugin.c)

## build the helper library used by the plugin
add_library(shadow-test-dynlink-lib SHARED shd-test-dynlink-lib.c)

## link the helper library to the plugin
target_link_libraries(shadow-test-plugin-dynlink shadow-test-dynlink-lib)

## build test executable
add_executable(shadow-test-dynlink shd-test-dynlink.c)
link_directories(${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(shadow-test-dynlink PROPERTIES INSTALL_RPATH ${CMAKE_CURRENT_BINARY_DIR} INSTALL_RPATH_USE_LINK_PATH TRUE LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_CURRENT_BINARY_DIR}")

## link the test exe to required libraries
target_link_libraries(shadow-test-dynlink ${DL_LIBRARIES} ${GLIB_LIBRARIES})

## register the test
add_test(NAME test-dynlink COMMAND shadow-test-dynlink)
