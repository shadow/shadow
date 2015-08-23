/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#define S_TO_NS 1000000000L
#define TOLERANCE 10000000L

typedef int (*sleep_cb_func)(void);

static int _call_sleep_cb() {
    return sleep(1);
}

static int _call_usleep_cb() {
    return usleep(1000000);
}

static int _call_nanosleep_cb() {
    struct timespec stop;
    stop.tv_sec = 1;
    stop.tv_nsec = 0;
    return nanosleep(&stop, NULL);
}

static int _sleep_run_test(sleep_cb_func sleep_f, const char* msg) {
    struct timespec start, end;

    /* get the start time */
    if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        return -1;
    }

    /* go to sleep for 1 second */
    if(sleep_f() != 0) {
        /* was interrupted */
        return -1;
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return -1;
    }

    /* let's make sure its within a small range (10ms) of 1 seconds */
    long long int diff = (long long int)(end.tv_nsec - start.tv_nsec);
    diff += (long long int)((end.tv_sec - start.tv_sec)*S_TO_NS);
    diff -= (long long int)(1*S_TO_NS);
    if( llabs(diff) > TOLERANCE) {
        fprintf(stdout, "%s failed. diff: %lli\n", msg, diff);
        return -1;
    }

    /* success! */
    return 0;
}

static int _test_sleep() {
    return _sleep_run_test(_call_sleep_cb, "sleep()");
}

static int _test_usleep() {
    return _sleep_run_test(_call_usleep_cb, "usleep()");
}

static int _test_nanosleep() {
    return _sleep_run_test(_call_nanosleep_cb, "nanosleep()");
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
