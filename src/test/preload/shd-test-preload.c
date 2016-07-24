/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>

int local_global_func(void) {
    printf("direct call to local_global_func()\n");
    return 0;
}

int run_test(void) {
    time_t t;

    t = time(NULL);
    printf("first time() called = %i\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was intercepted */
        return EXIT_FAILURE;
    }

    t = time(NULL);
    printf("second time() called = %i\n", (int)t);

    if(t == (time_t) -666666) {
        /* it was intercepted and forwarded to libc */
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
