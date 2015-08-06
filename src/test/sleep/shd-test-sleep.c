/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#define S_TO_NS 1000000000L

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

    /* let's make sure its within a small range (1ms) of 3 seconds */
    long diff = end.tv_nsec - start.tv_nsec;
    diff += (end.tv_sec - start.tv_sec)*S_TO_NS;
    diff -= 3*S_TO_NS;
    if( llabs(diff) > 1000000) {
        fprintf(stdout, "sleep failed. diff: %li\n", diff);
        return -1;
    }

    /* success! */
    return 0;
}

static int _test_usleep() {
    struct timespec start, end;

    /* get the start time */
    if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        return -1;
    }

    /* go to sleep for 30 microseconds */
    if(usleep(30) != 0) {
        /* was interrupted */
        return -1;
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return -1;
    }

    /* let's make sure its within a small range (1ms) of 3 microseconds */
    long diff = end.tv_nsec - start.tv_nsec;
    diff += (end.tv_sec - start.tv_sec)*S_TO_NS;
    diff -= 30000L;
    if( llabs(diff)> 1000000) {
        fprintf(stdout, "usleep failed. diff: %li", diff);
        return -1;
    }

    /* success! */
    return 0;
}

static int _test_nanosleep() {
    struct timespec start, stop, end;

    /* get the start time */
    if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        return -1;
    }

    /* set the stop time to 300 nanoseconds ahead */
    stop.tv_sec = 0;
    stop.tv_nsec = 300L;

    /* go to sleep for 300 nanoseconds */
    if(nanosleep(&stop, NULL) != 0) {
        /* was interrupted */
        return -1;
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return -1;
    }

    /* let's make sure its within a small range (5ns) of 300 nanoseconds */
    long diff = end.tv_nsec - start.tv_nsec;
    diff += (end.tv_sec - start.tv_sec)*S_TO_NS;
    diff -= 300L;
    if(labs(diff) < 5) {
        return -1;
    }

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
