add_library(shadow_openssl_rng SHARED shadow_openssl_rng.c)
target_compile_options(shadow_openssl_rng PRIVATE -D_GNU_SOURCE -fPIC)
install(TARGETS shadow_openssl_rng DESTINATION lib)
