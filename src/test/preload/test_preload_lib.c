/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <time.h>

time_t time (time_t *result){
    fprintf(stdout, "time lib called\n");
    return (time_t) 111111;
}

void set_call_next(int should_call_next) {
    fprintf(stdout, "set_call_next lib called - this should not happen if preload is working correctly\n");
}

void set_call_next2(int should_call_next) {
    fprintf(stdout, "set_call_next2 lib called - this should not happen if preload is working correctly\n");
}

void call_to_ensure_linkage() {
    fprintf(stdout, "call_to_ensure_linkage() called\n");
}
