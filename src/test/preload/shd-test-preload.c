/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>
#include <limits.h>

int local_global_func(void) {
    printf("direct call to local_global_func()\n");
    return 0;
}

int run_test(void) {
    time_t t;

    t = time(NULL);
    printf("first time() called, result = %i, expected = -666666\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was not intercepted */
        printf("test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    }

    t = time(NULL);
    printf("second time() called, result = %i, expected a unix timestamp\n", (int)t);

    if(t == (time_t) -666666) {
        /* it was intercepted and not forwarded to libc */
        printf("test failed because time() was not forwarded to libc\n");
        return EXIT_FAILURE;
    }

    if(t < 0 || t > INT_MAX) {
        printf("test failed because time() returned an out of range value\n");
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

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## preload test starting ##########\n");

    if(run_test() != 0) {
        fprintf(stdout, "########## preload test failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## preload test passed! ##########\n");
    return EXIT_SUCCESS;
}
