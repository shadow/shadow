/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/syscall.h>

#define S_TO_NS 1000000000L
#define TOLERANCE 10000000L

typedef int (*sleep_cb_func)(void);
typedef int (*clock_cb_func)(struct timespec*);

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

static int _call_clock_gettime_cb(struct timespec* ts) {
	return clock_gettime(CLOCK_MONOTONIC, ts);
}

static int _syscall_clock_gettime_cb(struct timespec* ts) {
	return syscall(SYS_clock_gettime, CLOCK_MONOTONIC, ts);
}

static int _sleep_run_test(sleep_cb_func sleep_f, clock_cb_func clock_f, const char* msg) {
    struct timespec start, end;

    /* get the start time */
    if(clock_f(&start) < 0) {
        return EXIT_FAILURE;
    }

    /* go to sleep for 1 second */
    if(sleep_f() != 0) {
        /* was interrupted */
        return EXIT_FAILURE;
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return EXIT_FAILURE;
    }

    /* let's make sure its within a small range (10ms) of 1 seconds */
    long long int diff = (long long int)(end.tv_nsec - start.tv_nsec);
    diff += (long long int)((end.tv_sec - start.tv_sec)*S_TO_NS);
    diff -= (long long int)(1*S_TO_NS);
    if( llabs(diff) > TOLERANCE) {
        fprintf(stdout, "%s failed. diff: %lli\n", msg, diff);
        return EXIT_FAILURE;
    }

    /* success! */
    return EXIT_SUCCESS;
}

static int _test_sleep() {
	int libc_result = _sleep_run_test(_call_sleep_cb, _call_clock_gettime_cb, "sleep()");
	int syscall_result = _sleep_run_test(_call_sleep_cb, _syscall_clock_gettime_cb, "sleep()");
    if(libc_result == EXIT_SUCCESS && syscall_result == EXIT_SUCCESS) {
    	return EXIT_SUCCESS;
    } else {
    	return EXIT_FAILURE;
    }
}

static int _test_usleep() {
    int libc_result = _sleep_run_test(_call_usleep_cb, _call_clock_gettime_cb, "usleep()");
	int syscall_result = _sleep_run_test(_call_usleep_cb, _syscall_clock_gettime_cb, "usleep()");
    if(libc_result == EXIT_SUCCESS && syscall_result == EXIT_SUCCESS) {
    	return EXIT_SUCCESS;
    } else {
    	return EXIT_FAILURE;
    }
}

static int _test_nanosleep() {
	int libc_result = _sleep_run_test(_call_nanosleep_cb, _call_clock_gettime_cb, "nanosleep()");
	int syscall_result = _sleep_run_test(_call_nanosleep_cb, _syscall_clock_gettime_cb, "nanosleep()");
    if(libc_result == EXIT_SUCCESS && syscall_result == EXIT_SUCCESS) {
    	return EXIT_SUCCESS;
    } else {
    	return EXIT_FAILURE;
    }
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## sleep test starting ##########\n");

    if(_test_sleep() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_sleep() failed\n");
        return EXIT_FAILURE;
    }

    if(_test_usleep() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_usleep() failed\n");
        return EXIT_FAILURE;
    }

    if(_test_nanosleep() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_nanosleep() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## sleep test passed! ##########\n");
    return EXIT_SUCCESS;
}
