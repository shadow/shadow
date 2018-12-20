/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>

static int test_randomOpenRead(const char *filename) {
    unsigned char buf[1];
    buf[0] = 0;

    int fd = open(filename, O_RDONLY);
    if(fd < 0) {
        return EXIT_FAILURE;
    }

    ssize_t sz = read(fd, buf, 1);
    if(sz != 1) {
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\t: %X\n", filename, *buf);
    close(fd);

    return EXIT_SUCCESS;
}

static int test_randomFOpenFRead(const char *filename) {
    unsigned char buf[1];
    buf[0] = 0;

    FILE *fp = fopen(filename, "r");
    if(fp == NULL) {
        return EXIT_FAILURE;
    }

    size_t sz = fread(buf, 1, 1, fp);
    if(sz != 1) {
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\t: %X\n", filename, *buf);

    fclose(fp);

    return EXIT_SUCCESS;
}

static int _test_fopen() {
    /* this should result in deterministic behavior */
    if(test_randomFOpenFRead("/dev/random") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    if(test_randomFOpenFRead("/dev/urandom") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
//    if(test_randomFOpenFRead("/dev/srandom") == EXIT_FAILURE) {
//        return EXIT_FAILURE;
//    }
    return EXIT_SUCCESS;
}

static int _test_open() {
    /* this should result in deterministic behavior */
    if(test_randomOpenRead("/dev/random") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    if(test_randomOpenRead("/dev/urandom") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
//    if(test_randomOpenRead("/dev/srandom") == EXIT_FAILURE) {
//        return EXIT_FAILURE;
//    }
    return EXIT_SUCCESS;
}

static int _test_getPID() {
    pid_t myPID = getpid();
    fprintf(stdout, "my process ID is %i\n", (int) myPID);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## determinism test starting ##########\n");

    fprintf(stdout, "starting _test_open()\n");
    if (_test_open() < 0) {
        fprintf(stdout, "########## _test_open() on random devices failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_open() passed\n");

    fprintf(stdout, "starting _test_fopen()\n");
    if (_test_fopen() < 0) {
        fprintf(stdout, "########## _test_fopen() on random devices failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_fopen() passed\n");

    fprintf(stdout, "starting _test_getPID()\n");
    if (_test_getPID() < 0) {
        fprintf(stdout, "########## _test_getPID() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_getPID() passed\n");

    fprintf(stdout, "########## determinism test passed! ##########\n");
    return EXIT_SUCCESS;
}
