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
void set_call_next2(int should_call_next);

int run_test(void) {
    time_t t;

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
    set_call_next2(0);

    fprintf(stdout, "second time() call\n");
    t = time(NULL);
    fprintf(stdout, "second time() called, result = %i, expected -888888\n", (int)t);

    if(t != (time_t) -888888) {
        /* it was not forwarded shd-test-preload-lib.c*/
        fprintf(stdout, "test failed because time() was not forwarded to interpose2\n");
        return EXIT_FAILURE;
    }

    set_call_next(1);
    set_call_next2(1);

    fprintf(stdout, "third time() call\n");
    t = time(NULL);
    fprintf(stdout, "third time() called, result = %i, expected = 111111\n", (int)t);

    if(t != (time_t) 111111) {
        /* it was not intercepted */
        fprintf(stdout, "test failed because time() was not properly intercepted\n");
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}
