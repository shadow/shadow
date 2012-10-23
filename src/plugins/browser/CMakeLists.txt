## Browser-Simulator

## Find libtidy which is needed to parse HTML
find_package(TIDY REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src/library/filetransfer/ ${TIDY_INCLUDES})

set(browser_sources
    shd-browser.c 
    shd-html.c
    shd-url.c
)

## service library to allow browser to be used by any plugin
add_library(shadow-service-browser STATIC ${browser_sources})
add_dependencies(shadow-service-browser shadow-util)
target_link_libraries(shadow-service-browser shadow-service-filetransfer ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${TIDY_LIBRARIES})

## the following two lines are needed if we want to allow external plug-ins to use ours
if(SHADOW_EXPORT STREQUAL ON)
    install(TARGETS shadow-service-browser EXPORT shadow-externals DESTINATION lib)
    install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
endif(SHADOW_EXPORT STREQUAL ON)

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-browser MODULE shd-browser-plugin.c ${browser_sources})
target_link_libraries(shadow-plugin-browser shadow-service-filetransfer ${TIDY_LIBRARIES})
install(TARGETS shadow-plugin-browser DESTINATION ${SHADOW_PLUGIN_DIR})

## executable that can run outside of shadow
add_executable(shadow-browser shd-browser-main.c)
target_link_libraries(shadow-browser shadow-service-filetransfer shadow-service-browser ${RT_LIBRARIES} ${GLIB_LIBRARIES} ${TIDY_LIBRARIES})
install(TARGETS shadow-browser DESTINATION bin)

