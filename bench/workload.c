#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#define N_ITR 10000
#define WORK_ITR 100

static volatile float x_ = 0;

static void print_string_(const char *str) {
    write(1, str, strlen(str));
}

static void print_current_time_() {
    time_t now = {0};
    struct tm *info = NULL;
    time(&now);
    info = localtime(&now);
    print_string_(asctime(info));
}

static inline void work_(size_t i) {

    float i_f = (float)i,
          acc = 0.0f;

    for (size_t idx = 0; idx < WORK_ITR; ++idx) {
        float idx_f = (float)idx;
        acc += sin(idx_f + i_f);
    }

    x_ += acc;
}

int main(int argc, char **argv) {

    print_current_time_();

    struct timespec sleep_tm = { 0, 1000000 }; // 1,000,000 ns = 1 ms

    for (size_t idx = 0; idx < N_ITR; ++idx) {
        work_(idx);
        nanosleep(&sleep_tm, NULL);
    }

    print_current_time_();
    return 0;
}
