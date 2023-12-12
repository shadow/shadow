# Set the name of the lib for convenience.
set(LIB_NAME shadow_injector)

# Create a shared library that will be preloaded.
add_library(${LIB_NAME} SHARED injector.c)

# Link to the shim so the shim gets injected into the managed process.
target_link_libraries(${LIB_NAME} shadow-shim)
