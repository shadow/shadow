/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>
#include <limits.h>

void set_call_next(int should_call_next);

int local_global_func(void) {
    printf("direct call to local_global_func()\n");
    return 0;
}

int run_test(void) {
    time_t t;

    set_call_next(0);

    printf("first time() call\n");
    t = time(NULL);
    printf("first time() called, result = %i, expected = -666666\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was not intercepted */
        printf("test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    }

    set_call_next(1);

    printf("second time() call\n");
    t = time(NULL);
    printf("second time() called, result = %i, expected 111111\n", (int)t);

    if(t != (time_t) 111111) {
        /* it was not forwarded shd-test-preload-lib.c*/
        printf("test failed because time() was not forwarded to shd-test-preload-lib.c\n");
        return EXIT_FAILURE;
    }

    set_call_next(0);

    printf("third time() call\n");
    t = time(NULL);
    printf("third time() called, result = %i, expected = -666666\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was not intercepted */
        printf("test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    }

    set_call_next(1);

    printf("fourth time() call\n");
    t = time(NULL);
    printf("fourth time() called, result = %i, expected 111111\n", (int)t);

    if(t != (time_t) 111111) {
        /* it was not forwarded shd-test-preload-lib.c*/
        printf("test failed because time() was not forwarded to shd-test-preload-lib.c\n");
        return EXIT_FAILURE;
    }

    int retval = local_global_func();

    if(retval == 0) {
        /* our local func was called instead of the preloaded version.
         * this is expected when called from an exe.
         * interception can only occur when called from a library.so type */
        return EXIT_SUCCESS;
    } else {
        /* it was unexpectedly intercepted */
        printf("test failed, local global function was unexpectedly intercepted\n");
        return EXIT_FAILURE;
    }
}

