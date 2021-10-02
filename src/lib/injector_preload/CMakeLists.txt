# Set the name of the lib for convenience.
set(LIB_NAME shadow_injector)

# Create a shared library that will be preloaded.
add_library(${LIB_NAME} SHARED injector.c)

# Position-independent so we can preload it.
target_compile_options(${LIB_NAME} PRIVATE -D_GNU_SOURCE -fPIC)

# Link to the shim so the shim gets injected into the managed process.
target_link_libraries(${LIB_NAME} shadow-shim)

# We install the library so we can preload it at runtime.
install(TARGETS ${LIB_NAME} DESTINATION lib)
