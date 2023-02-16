## link to preload libs (rpath will not contain these paths when shadow is installed)
link_directories(${CMAKE_BINARY_DIR}/src/lib/shim)
link_directories(${CMAKE_BINARY_DIR}/src/lib/shmem)
link_directories(${CMAKE_BINARY_DIR}/src/lib/injector_preload)
link_directories(${CMAKE_BINARY_DIR}/src/lib/libc_preload)
link_directories(${CMAKE_BINARY_DIR}/src/lib/openssl_preload)

## compile defs and flags
add_definitions(-D_GNU_SOURCE)

## specify the main shadow executable, build, link, and install
add_executable(shadow main.c)
target_link_libraries(shadow shadow-rs shadow-shmem shadow-tsc logger PkgConfig::GLIB ${CMAKE_THREAD_LIBS_INIT} ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} -lstdc++)
install(TARGETS shadow DESTINATION bin)

## shadow needs to find libs after install
set_target_properties(shadow PROPERTIES LINK_FLAGS "-Wl,--no-as-needed")