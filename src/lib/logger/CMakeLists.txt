add_cflags(-fPIC)

add_library(logger STATIC
    logger.c
    log_level.c)
target_link_libraries(logger PkgConfig::GLIB ${CMAKE_THREAD_LIBS_INIT})

add_subdirectory(rust_bindings)
