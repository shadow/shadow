/*
 * shd-test-preload-dlsym.c
 *
 *  Created on: Oct 29, 2016
 *      Author: rob
 */

#include <stdio.h>

extern void* do_lookup(char* funcname);

void __attribute__((constructor)) construct() {
    /* test if we can do a dlsym() lookup in the constructor */
    if(do_lookup("time") == NULL) {
        fprintf(stdout, "failed to load time() in constructor\n");
    } else {
        fprintf(stdout, "succeeded loading time() in constructor\n");
    }
    if(do_lookup("malloc") == NULL) {
        fprintf(stdout, "failed to load malloc() in constructor\n");
    } else {
        fprintf(stdout, "succeeded loading malloc() in constructor\n");
    }
}
