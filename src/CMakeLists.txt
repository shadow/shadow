# Outside of external, inclusion of headers within the project should be
# relative to this directory.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

## check for dependencies with our own Find scripts in "./cmake"
## these don't have built-in cmake files in /usr/share/cmake/Modules/Findxxx.cmake
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)
find_package(PROCPS REQUIRED)

## pthreads
set(CMAKE_THREAD_PREFER_PTHREAD 1)
find_package(Threads REQUIRED)
message(STATUS "Using Threads library: ${CMAKE_THREAD_LIBS_INIT}")

# Build support libraries.
add_subdirectory(support)

## build the shim
add_subdirectory(shim)

## build the core simulator
add_subdirectory(main)

if(SHADOW_TEST STREQUAL ON)
    add_subdirectory(test)
endif(SHADOW_TEST STREQUAL ON)

# A fake target that depends on all bindings.
add_custom_target(bindings)
add_dependencies(bindings bindings_main)

# Only re-generate bindings when explicititly requested, so that
# our CI doesn't need to install the heavy bindgen dependency.
set_property(TARGET bindings PROPERTY EXCLUDE_FROM_ALL true)
