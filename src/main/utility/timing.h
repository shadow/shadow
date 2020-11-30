#ifndef TIMING_H_
#define TIMING_H_
#include <stdio.h>
#include <time.h>

#define RECORD_TIME(TS_NAME) \
    struct timespec TS_NAME; \
    clock_gettime(CLOCK_REALTIME, &TS_NAME);

static inline double timespec_to_seconds(const struct timespec *t) {
    return (double)t->tv_sec + (double)t->tv_nsec * 1E-9;
}

static inline double duration_to_seconds(const struct timespec *t0,
                           const struct timespec *t1) {
    return timespec_to_seconds(t1) - timespec_to_seconds(t0);
}

static inline void print_durations() {
    extern double FORK_DURATION_ACC;
    fprintf(stderr, "Fork duration: %f seconds.\n", FORK_DURATION_ACC);
}

#endif // TIMING_H_
