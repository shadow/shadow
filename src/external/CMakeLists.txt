## we will use cmake's external module feature to build our external projects
## http://www.kitware.com/media/html/BuildingExternalProjectsWithCMake2.8.html
INCLUDE(ExternalProject)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RPTH_DEBUG_SWITCH "--enable-debug")
else()
    set(RPTH_DEBUG_SWITCH "--disable-debug")
endif()

EXTERNALPROJECT_ADD(
    "rpth"
    PREFIX rpth
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rpth
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/rpth
    CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rpth/configure --quiet --prefix=${CMAKE_BINARY_DIR} --disable-shared --disable-tests ${RPTH_DEBUG_SWITCH}
    CFLAGS=-Wno-unknown-warning-option
    BUILD_COMMAND make
    BUILD_IN_SOURCE 0
    INSTALL_COMMAND ""
)

#ADD_LIBRARY(rpth-imp STATIC IMPORTED)
#SET_TARGET_PROPERTIES(rpth PROPERTIES
#    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/rpth/rpth${CMAKE_STATIC_LIBRARY_SUFFIX}"
#    IMPORTED_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/rpth/librpth.la"
#)
#add_dependencies(rpth rpth-ext)