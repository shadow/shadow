/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// The number of random values to generate with each method.
#define RGENLEN 100
// The number of buckets to use when checking random value distribution.
#define BUCKETLEN 10

static int _check_randomness(double* fracs, int len) {
    uint buckets[BUCKETLEN] = {0};

    for (int i = 0; i < len; i++) {
        uint percent = (uint)(fracs[i] * 100.0f);
        assert(percent >= 0 && percent <= 100);
        int j = percent % BUCKETLEN;
        buckets[j]++;
    }

    int fail = 0;
    fprintf(stdout, "bucket values:\n");
    for (int i = 0; i < BUCKETLEN; i++) {
        fprintf(stdout, "bucket[%d] = %u\n", i, buckets[i]);
        if (buckets[i] == 0) {
            fail = 1;
        }
    }

    if (fail) {
        fprintf(stdout, "failed to get random values across entire range\n");
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

static int _test_path_helper(const char* path) {
    int fd = open(path, O_RDONLY);

    double values[RGENLEN] = {0};

    for (int i = 0; i < RGENLEN; i++) {
        uint random_value = 0;

        if (read(fd, &random_value, sizeof(uint)) < 0) {
            fprintf(stdout, "error: unable to read random data from %s\n", path);
            close(fd);
            return EXIT_FAILURE;
        }

        values[i] = (double)random_value / UINT_MAX;
    }

    close(fd);

    return _check_randomness(values, RGENLEN);
}

static int _test_dev_urandom() { return _test_path_helper("/dev/urandom"); }

static int _test_dev_random() { return _test_path_helper("/dev/random"); }

static int _test_rand() {
    double values[RGENLEN] = {0};

    for (int i = 0; i < RGENLEN; i++) {
        int random_value = rand();

        if (random_value < 0 || random_value > RAND_MAX) {
            fprintf(stdout, "error: rand returned bytes outside of expected range\n");
            return EXIT_FAILURE;
        }

        values[i] = (double)random_value / RAND_MAX;
    }

    return _check_randomness(values, RGENLEN);
}

static int _test_getrandom() {
    double values[RGENLEN] = {0};

    for (int i = 0; i < RGENLEN; i++) {
        uint random_value = 0;

        // getrandom() was only added in glibc 2.25, so use syscall until all of
        // our supported OS targets pick up the new libc call
        // https://sourceware.org/legacy-ml/libc-alpha/2017-02/msg00079.html
        if (syscall(SYS_getrandom, &random_value, sizeof(uint), 0) <= 0) {
            fprintf(stdout, "error: rand returned bytes outside of expected range\n");
            return EXIT_FAILURE;
        }

        values[i] = (double)random_value / UINT_MAX;
    }

    return _check_randomness(values, RGENLEN);
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## random test starting ##########\n");

    fprintf(stdout, "########## starting _test_dev_random()\n");
    if (_test_dev_random() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_dev_random() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "########## starting _test_dev_urandom()\n");
    if (_test_dev_urandom() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_dev_urandom() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "########## starting _test_rand()\n");
    if (_test_rand() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_rand() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "########## starting _test_getrandom()\n");
    if (_test_getrandom() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_getrandom() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## random test passed! ##########\n");
    return EXIT_SUCCESS;
}
