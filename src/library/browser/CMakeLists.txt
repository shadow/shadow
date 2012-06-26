## Browser-Simulator
add_definitions(-D_GNU_SOURCE)

## Find libtidy which is needed to parse HTML
find_package(TIDY REQUIRED)

include_directories(${SHADOW_SOURCE_DIR}/library/filetransfer/ ${TIDY_INCLUDE_DIR})

## a stand-alone shared library that can plug into shadow
add_library(shadow-plugin-browser MODULE shd-browser-plugin.c shd-browser.c shd-url.c shd-html.c)
target_link_libraries(shadow-plugin-browser shadow-service-filetransfer ${TIDY_LIBRARIES})
install(TARGETS shadow-plugin-browser DESTINATION ${SHADOW_PLUGIN_DIR})

install(DIRECTORY ./ DESTINATION include/ FILES_MATCHING PATTERN "*.h")
