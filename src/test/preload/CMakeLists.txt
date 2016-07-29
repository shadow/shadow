## build the preload library
add_library(shadow-test-preload-lib SHARED shd-test-preload-lib.c)
target_link_libraries(shadow-test-preload-lib ${DL_LIBRARIES})

## build test executable
add_shadow_exe(shadow-test-preload shd-test-preload.c)

## register the test
add_test(NAME test-preload 
         COMMAND shadow-test-preload)
SET_TESTS_PROPERTIES(test-preload
    PROPERTIES ENVIRONMENT "LD_PRELOAD=${CMAKE_CURRENT_BINARY_DIR}/libshadow-test-preload-lib.so")