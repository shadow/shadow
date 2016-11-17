/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdlib.h>

extern int run_test(void);

int local_global_func(void) {
    printf("direct call to local_global_func()\n");
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## preload test starting ##########\n");

    int retval = local_global_func();

    /* our local func should be called instead of the preloaded version.
     * this is expected when called from an exe.
     * interception can only occur when called from a library.so type */
    if(retval != 0) {
        /* it was unexpectedly intercepted */
        printf("test failed, local global function was unexpectedly intercepted\n");
        fprintf(stdout, "########## preload test failed\n");
        return EXIT_FAILURE;
    }

    if(run_test() != 0) {
        fprintf(stdout, "########## preload test failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## preload test passed! ##########\n");
    return EXIT_SUCCESS;
}
