add_definitions(-D_GNU_SOURCE)

# fileserver plug-in
add_library(shadow-plugin-fileserver MODULE shd-plugin-fileserver.c shd-fileserver.c 
    ${SHADOW_SOURCE_DIR}/util/hashtable.c 
    ${SHADOW_SOURCE_DIR}/util/hash.c 
    ${SHADOW_SOURCE_DIR}/util/btree.c)
target_link_libraries(shadow-plugin-fileserver shadow-snri)

# filegetter plug-in
add_library(shadow-plugin-filegetter MODULE shd-plugin-filegetter.c shd-plugin-filegetter-util.c shd-service-filegetter.c shd-filegetter.c
    ${SHADOW_SOURCE_DIR}/util/orderedlist.c
    ${SHADOW_SOURCE_DIR}/util/shd-cdf.c)
target_link_libraries(shadow-plugin-filegetter shadow-snri rt)

# fileserver executable
add_executable(shadow-fileserver shd-fileserver-main.c shd-fileserver.c)
target_link_libraries(shadow-fileserver shadow-util)

# filegetter executable
add_executable(shadow-filegetter shd-filegetter-main.c shd-service-filegetter.c shd-filegetter.c 
    ${SHADOW_SOURCE_DIR}/util/shd-cdf.c 
    ${SHADOW_SOURCE_DIR}/util/orderedlist.c)
target_link_libraries(shadow-filegetter rt)

add_dependencies(shadow-plugin-fileserver shadow-snri shadow-util)
add_dependencies(shadow-plugin-filegetter shadow-snri)