add_definitions(-D_GNU_SOURCE)

## fileserver executable
add_executable(shadow-fileserver shd-fileserver-main.c shd-fileserver.c)
target_link_libraries(shadow-fileserver shadow-util)

## fileserver plug-in
add_library(shadow-plugin-fileserver MODULE shd-plugin-fileserver.c shd-fileserver.c)
target_link_libraries(shadow-plugin-fileserver shadow-plugin shadow-util)

## filegetter executable
add_executable(shadow-filegetter shd-filegetter-main.c shd-service-filegetter.c shd-filegetter.c)
target_link_libraries(shadow-filegetter shadow-util rt)

## filegetter plug-in
add_library(shadow-plugin-filegetter MODULE shd-plugin-filegetter.c shd-plugin-filegetter-util.c shd-service-filegetter.c shd-filegetter.c)
target_link_libraries(shadow-plugin-filegetter shadow-plugin shadow-util rt)

## external libraries
add_library(shadow-plugin-service STATIC shd-fileserver.c shd-plugin-filegetter-util.c shd-service-filegetter.c shd-filegetter.c)
target_link_libraries(shadow-plugin-service shadow-util rt)

add_dependencies(shadow-plugin-fileserver shadow-plugin shadow-util)
add_dependencies(shadow-plugin-filegetter shadow-plugin shadow-util)
add_dependencies(shadow-plugin-service shadow-plugin shadow-util)

install(TARGETS shadow-fileserver shadow-filegetter DESTINATION bin)
install(TARGETS shadow-plugin-fileserver shadow-plugin-filegetter DESTINATION lib)
install(TARGETS shadow-plugin-service EXPORT shadow-externals DESTINATION lib)
install(DIRECTORY ./ DESTINATION include/shadow FILES_MATCHING PATTERN "*.h")
install(FILES downloads.spec filetransfer.dsim DESTINATION share/shadow)
