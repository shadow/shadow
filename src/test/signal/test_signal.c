/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

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

    pid_t pid = syscall(SYS_getpid);
    pid_t tid = syscall(SYS_gettid);

    if (syscall(SYS_kill, pid, 0) < 0) {
        fprintf(stdout, "kill(%li,0) has error %i: %s\n", (long)pid, errno, strerror(errno));
        fprintf(stdout, "########## signal test failed ##########\n");
        return EXIT_FAILURE;
    }

    if (syscall(SYS_tkill, tid, 0) < 0) {
        fprintf(stdout, "tkill(%li,0) has error %i: %s\n", (long)tid, errno, strerror(errno));
        fprintf(stdout, "########## signal test failed ##########\n");
        return EXIT_FAILURE;
    }

    if (syscall(SYS_tgkill, pid, tid, 0) < 0) {
        fprintf(stdout, "tgkill(%li,%li,0) has error %i: %s\n", (long)pid, (long)tid, errno,
                strerror(errno));
        fprintf(stdout, "########## signal test failed ##########\n");
        return EXIT_FAILURE;
    }

    struct sigaction action;
    action.sa_handler = signal_handled_func;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    /* handle the signal */
    sigaction(SIGUSR1, &action, NULL);

    /* trigger the signal */
    kill(pid, SIGUSR1);

    /* they should have been handled */
    fprintf(stdout, "signals were not handled\n");
    fprintf(stdout, "########## signal test failed ##########\n");
    return EXIT_FAILURE;
}
