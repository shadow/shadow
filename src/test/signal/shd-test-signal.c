/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*func_t)(int arg);
int signal_handled_count = 0;

void signal_handled_func(int signum) {
    signal_handled_count++;
    if(signal_handled_count == 1) {
        fprintf(stdout, "########## signal test passed! ##########\n");
        exit(EXIT_SUCCESS);
    } else {
        fprintf(stdout, "signal handle count is %d, expected 1\n", signal_handled_count);
        fprintf(stdout, "########## signal test failed ##########\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## signal test starting ##########\n");

    struct sigaction action;
    action.sa_handler = signal_handled_func;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    /* handle the signal */
    sigaction(SIGSEGV, &action, NULL);

    /* trigger the signal */
    func_t func = NULL;
    func(128); // SIGSEGV

    /* they should have been handled */
    fprintf(stdout, "signals were not handled\n");
    fprintf(stdout, "########## signal test failed ##########\n");
    return EXIT_FAILURE;
}
