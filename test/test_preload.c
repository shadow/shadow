/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>

int main(void) {
    time_t t;

    t = time(NULL);
    printf("first time() called = %i\n", (int)t);

    t = time(NULL);
    printf("second time() called = %i\n", (int)t);

    return EXIT_SUCCESS;
}
