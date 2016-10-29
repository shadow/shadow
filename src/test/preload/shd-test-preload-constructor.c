/*
 * shd-test-preload-dlsym.c
 *
 *  Created on: Oct 29, 2016
 *      Author: rob
 */

#include <stdio.h>

extern void* lookup_time();

void __attribute__((constructor)) construct() {
    /* test if we can do a dlsym() lookup in the constructor */
    if(lookup_time() == NULL) {
        printf("failed to load time() in constructor\n");
    }
}
