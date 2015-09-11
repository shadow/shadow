/*
 * gcc -o test_preload test_preload.c
 * LD_PRELOAD=`pwd`/test_preload_lib.so ./test_preload
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>

void local_global_func(void) {
    printf("direct call to local_global_func()\n");
}

int main(void) {
    time_t t;

    t = time(NULL);
    printf("first time() called = %i\n", (int)t);

    t = time(NULL);
    printf("second time() called = %i\n", (int)t);

    local_global_func();

    return EXIT_SUCCESS;
}
