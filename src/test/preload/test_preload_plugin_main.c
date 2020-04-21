/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdlib.h>

extern int run_test_arg(time_t);

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## preload test starting ##########\n");

    /* preload.test.shadow.config.xml runs at time 946684803, so shadow's preload will return 946684803 */
    if(run_test_arg((time_t)946684803) != 0) {
        fprintf(stdout, "########## preload test failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## preload test passed! ##########\n");
    return EXIT_SUCCESS;
}
