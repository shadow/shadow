include(FindPkgConfig)

# Outside of external, inclusion of headers within the project should be
# relative to this directory.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.32)

## pthreads
set(CMAKE_THREAD_PREFER_PTHREAD 1)
find_package(Threads REQUIRED)
message(STATUS "Using Threads library: ${CMAKE_THREAD_LIBS_INIT}")

# Build support libraries.
add_subdirectory(lib)

## build the core simulator
add_subdirectory(main)

if(SHADOW_TEST STREQUAL ON)
    add_subdirectory(test)
endif(SHADOW_TEST STREQUAL ON)

# A fake target that depends on all bindings.
add_custom_target(bindings)
add_dependencies(bindings bindings_main)
add_dependencies(bindings bindings_logger)
add_dependencies(bindings bindings_objgraph_c)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings PROPERTY EXCLUDE_FROM_ALL true)
