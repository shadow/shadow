include_directories(${GLIB_INCLUDE_DIRS})

add_executable(tsc_test tsc_test.c)
target_link_libraries(tsc_test asm-util logger ${GLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} -ldl)
add_test(NAME tsc_test COMMAND tsc_test)
