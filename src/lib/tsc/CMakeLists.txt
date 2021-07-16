include_directories(${GLIB_INCLUDES})

add_library(shadow-tsc STATIC tsc.c)
target_compile_options(shadow-tsc PRIVATE -D_GNU_SOURCE -fPIC)
target_link_libraries(shadow-tsc logger)

add_executable(tsc_test tsc_test.c)
target_link_libraries(tsc_test ${GLIB_LIBRARIES} shadow-tsc)
add_test(NAME tsc_test COMMAND tsc_test)
