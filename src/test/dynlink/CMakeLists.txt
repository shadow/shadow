find_package(GLIB REQUIRED)
include_directories(${DL_INCLUDES} ${GLIB_INCLUDES})

add_cflags("-ftls-model=global-dynamic")
add_definitions(-ftls-model=global-dynamic)

## build the dynamically loaded plugin
add_library(shadow-test-dynlink-plugin SHARED shd-test-dynlink-plugin.c)

## build the helper library used by the plugin
add_library(shadow-test-dynlink-lib SHARED shd-test-dynlink-lib.c)

## link the helper library to the plugin
target_link_libraries(shadow-test-dynlink-plugin shadow-test-dynlink-lib)

## build test executable
add_executable(shadow-test-dynlink shd-test-dynlink.c shd-test-dynlink-main.c)
link_directories(${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(shadow-test-dynlink PROPERTIES 
    INSTALL_RPATH ${CMAKE_BINARY_DIR}/src/external/elf-loader
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-Wl,--no-as-needed,-rpath=${CMAKE_BINARY_DIR}/src/external/elf-loader,-dynamic-linker=${CMAKE_BINARY_DIR}/src/external/elf-loader/ldso"
)

## link the test exe to required libraries
target_link_libraries(shadow-test-dynlink ${DL_LIBRARIES} ${GLIB_LIBRARIES})

## build .so test for running in shadow
add_library(shadow-plugin-test-dynlink SHARED shd-test-dynlink.c shd-test-dynlink-main-shadow.c)
target_link_libraries(shadow-plugin-test-dynlink ${DL_LIBRARIES} ${GLIB_LIBRARIES})

## register the test
add_test(NAME dynlink COMMAND shadow-test-dynlink)
add_test(NAME dynlink-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d dynlink.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/dynlink.test.shadow.config.xml)

##########################
## To do this manually: ##
##########################
## Link the lib, plugin, and executable; then run the test
# gcc -shared -Wl,-soname,libshadow-test-dynlink-lib.so -fPIC -ftls-model=global-dynamic -o libshadow-test-dynlink-lib.so shd-test-dynlink-lib.c
# gcc -L`pwd` -shared -Wl,-rpath=`pwd`,-soname,libshadow-test-dynlink-plugin.so -fPIC -ftls-model=global-dynamic -o libshadow-test-dynlink-plugin.so shd-test-dynlink-plugin.c -lshadow-test-dynlink-lib
# gcc -Wl,-rpath=`pwd` -fPIC -ftls-model=global-dynamic `pkg-config --cflags glib-2.0 gmodule-2.0` -o shd-test-dynlink shd-test-dynlink.c `pkg-config --libs glib-2.0 gmodule-2.0` -ldl -lpthread
# ./shd-test-dynlink
##
## important links ##
# https://sourceware.org/bugzilla/show_bug.cgi?id=11787
# https://bugzilla.redhat.com/show_bug.cgi?id=1124987
# http://stackoverflow.com/questions/19268293/matlab-error-cannot-open-with-static-tls
# http://stackoverflow.com/questions/14892101/cannot-load-any-more-object-with-static-tls
# https://github.com/lattera/glibc/blob/master/sysdeps/generic/ldsodefs.h#L490
