## check for dependencies, using built-in cmake files to help us find the libs
## these are in /usr/share/cmake/Modules/Findxxx.cmake
find_package(ZLIB REQUIRED)

## other libs dont have a Findxxx.cmake, so we check on our own (./cmake)
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(OPENSSL REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(GLIB REQUIRED)

#find_package(TOR REQUIRED)
get_filename_component(CURRENT_TOR_DIR "${CMAKE_BINARY_DIR}/../tor" ABSOLUTE)
include_directories(${CURRENT_TOR_DIR})
include_directories(${CURRENT_TOR_DIR}/src)
include_directories(${CURRENT_TOR_DIR}/src/common)
include_directories(${CURRENT_TOR_DIR}/src/or)

include_directories(AFTER ${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${EVENT2_INCLUDES} ${OPENSSL_INCLUDES} ${TOR_INCLUDES} ${SHADOW_INCLUDES})

## import shadow libs
#INCLUDE(${CMAKE_INSTALL_PREFIX}/share/shadow/shadow-externals.cmake)

## get tor source files, but ignore some of them
aux_source_directory(${CURRENT_TOR_DIR}/src/common torcommon_sources)
list(REMOVE_ITEM torcommon_sources 
    ${CURRENT_TOR_DIR}/src/common/sha256.c
    ${CURRENT_TOR_DIR}/src/common/strlcat.c
    ${CURRENT_TOR_DIR}/src/common/strlcpy.c
)
aux_source_directory(${CURRENT_TOR_DIR}/src/or toror_sources)
list(REMOVE_ITEM toror_sources 
    ${CURRENT_TOR_DIR}/src/or/ntmain.c
    ${CURRENT_TOR_DIR}/src/or/tor_main.c
)

## get tor header files -- we dont need this for now
#file(GLOB tor_headers ${CURRENT_TOR_DIR}/*.h)
#file(GLOB toror_headers ${CURRENT_TOR_DIR}/src/or/*.h)
#file(GLOB torcommon_headers ${CURRENT_TOR_DIR}/src/common/*.h)
#list(APPEND tor_headers ${toror_headers} ${torcommon_headers})
#foreach(headerpath ${tor_headers})
#    get_filename_component(headername ${headerpath} NAME)
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${headername}")
#endforeach(headerpath)

## tor needs these defined
add_definitions(-DLOCALSTATEDIR="/usr/local/var" -DSHARE_DATADIR="/usr/local/var" -DBINDIR="/usr/local/bin")
remove_definitions(-DNDEBUG)

## create and install a shared library that can plug into shadow
add_bitcode(shadow-plugin-scallion-bitcode 
    shd-scallion-plugin.c shd-scallion.c ${toror_sources} ${torcommon_sources})
add_plugin(shadow-plugin-scallion shadow-plugin-scallion-bitcode)

target_link_libraries(shadow-plugin-scallion
    ${TOR_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} 
    ${ZLIB_LIBRARIES} ${M_LIBRARIES} ${RT_LIBRARIES} ${DL_LIBRARIES})

install(TARGETS shadow-plugin-scallion DESTINATION plugins)
set_target_properties(shadow-plugin-scallion PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE)

## preload library for intercepting functions
add_library(shadow-preload-scallion SHARED shd-scallion-preload.c)
target_link_libraries(shadow-preload-scallion ${GLIB_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-preload-scallion DESTINATION lib)

## install scallion script
install(PROGRAMS scallion DESTINATION bin)

install(FILES ${CURRENT_TOR_DIR}/src/config/geoip DESTINATION share/)
#install(DIRECTORY resource/ DESTINATION share PATTERN "config/*")
