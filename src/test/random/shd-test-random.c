/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#define LOW_THRESH 0.1f
#define HIGH_THRESH 0.9f

static int _test_dev_urandom() {
    const char* path = "/dev/urandom";
    uint numLow = 0, numHigh = 0;

    int fd = open(path, O_RDONLY);
    for(int i = 0; i < 100; i++) {
        uint randomValue = 0;

        if(read(fd, &randomValue, sizeof(uint)) < 0) {
            fprintf(stdout, "error: unable to read random data from %s\n", path);
            close(fd);
            return -1;
        }

        double randomFrac = ((double)randomValue) / ((double)UINT_MAX);
        fprintf(stdout, "########## random_frac is %f\n", randomFrac);
        if(randomFrac < LOW_THRESH) {
            numLow++;
        } else if(randomFrac > HIGH_THRESH) {
            numHigh++;
        }
    }
    close(fd);

    fprintf(stdout, "got %u low and %u high values from %s\n", numLow, numHigh, path);

    if(numLow > 0 && numHigh > 0) {
        /* success! */
        return 0;
    } else{
        return -1;
    }
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## random test starting ##########\n");

    if(_test_dev_urandom() < 0) {
        fprintf(stdout, "########## _test_dev_urandom() failed\n");
        return -1;
    }

    fprintf(stdout, "########## random test passed! ##########\n");
    return 0;
}
