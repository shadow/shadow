add_cflags(-fPIC)
add_library(logger STATIC
    logger.c
    log_level.c)
target_include_directories(logger PRIVATE ${GLIB_INCLUDES})
target_link_libraries(logger ${GLIB_LIBRARIES} pthread)
