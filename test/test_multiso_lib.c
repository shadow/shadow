#include <stdio.h>

int global = 1;

void test_function(void) {
    global++;

    printf("Global: %d\n", global);

    return;
}

