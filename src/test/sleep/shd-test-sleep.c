/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>

static int _test_sleep() {
    struct timespec start, end;

    /* get the start time */
    if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        return -1;
    }

    /* go to sleep for 3 seconds */
    if(sleep(3) != 0) {
        /* was interrupted */
        return -1;
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return -1;
    }

    /* lets make sure its within a small range of 3 seconds */
    // TODO

    /* success! */
    return 0;
}

static int _test_usleep() {
    // TODO

    /* success! */
    return 0;
}

static int _test_nanosleep() {
    // TODO

    /* success! */
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## sleep test starting ##########\n");

    if(_test_sleep() < 0) {
        fprintf(stdout, "########## _test_sleep() failed\n");
        return -1;
    }

    if(_test_usleep() < 0) {
        fprintf(stdout, "########## _test_usleep() failed\n");
        return -1;
    }

    if(_test_nanosleep() < 0) {
        fprintf(stdout, "########## _test_nanosleep() failed\n");
        return -1;
    }

    fprintf(stdout, "########## sleep test passed! ##########\n");
    return 0;
}
