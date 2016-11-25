/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>
#include <limits.h>

void call_to_ensure_linkage();
void set_call_next(int should_call_next);

int run_test_arg(time_t next_time_result) {
    time_t t;

    call_to_ensure_linkage();

    set_call_next(0);

    fprintf(stdout, "first time() call\n");
    t = time(NULL);
    fprintf(stdout, "first time() called, result = %i, expected = -666666\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was not intercepted */
        fprintf(stdout, "test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    }

    set_call_next(1);

    fprintf(stdout, "second time() call\n");
    t = time(NULL);
    fprintf(stdout, "second time() called, result = %i, expected 111111\n", (int)t);

    if(t != (time_t) next_time_result) {
        /* it was not forwarded shd-test-preload-lib.c*/
        fprintf(stdout, "test failed because time() was not forwarded to shd-test-preload-lib.c\n");
        return EXIT_FAILURE;
    }

    set_call_next(0);

    fprintf(stdout, "third time() call\n");
    t = time(NULL);
    fprintf(stdout, "third time() called, result = %i, expected = -666666\n", (int)t);

    if(t != (time_t) -666666) {
        /* it was not intercepted */
        fprintf(stdout, "test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    }

    set_call_next(1);

    fprintf(stdout, "fourth time() call\n");
    t = time(NULL);
    fprintf(stdout, "fourth time() called, result = %i, expected 111111\n", (int)t);

    if(t != (time_t) next_time_result) {
        /* it was not forwarded shd-test-preload-lib.c*/
        fprintf(stdout, "test failed because time() was not forwarded to shd-test-preload-lib.c\n");
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

int run_test(void) {
    run_test_arg((time_t)111111);
}
