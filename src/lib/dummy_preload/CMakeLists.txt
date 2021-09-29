# Normally, the shim is linked to the openssl or libc preload libs and the shim
# will be injected when those libs are preloaded. However, if the preloading of
# both of those libs is disabled, then we inject the dummy lib instead (which is
# also linked to the shim) in order to ensure that the shim is still loaded into
# the managed process space as required for Shadow to work properly.
add_library(shadow_dummy SHARED dummy.c)

# Position-independent so we can preload it.
target_compile_options(shadow_dummy PRIVATE -D_GNU_SOURCE -fPIC)

# Link to the shim so the shim gets injected into the managed process.
target_link_libraries(shadow_dummy shadow-shim)

# We install the dummy library so we can preload it.
install(TARGETS shadow_dummy DESTINATION lib)
