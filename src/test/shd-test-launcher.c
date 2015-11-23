/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 * Launches multiple processes as separated by a ':' arg. For example, the arg string:
 *   "shadow-test-launcher shadow-plugin-test-tcp blocking server : shadow-plugin-test-tcp blocking client 127.0.0.1"
 * would result in two child processes:
 *   "shadow-plugin-test-tcp blocking server"
 * and
 *   "shadow-plugin-test-tcp blocking client 127.0.0.1"
 */
int main(int argc, char *argv[]) {
    char* childargs[argc];
    int j = 0;
    pid_t childids[argc];
    int k = 0;

    /* parse out the args and launch the children */
    for(int i = 1; i < argc; i++) { // skip path to this program
        childargs[j] = argv[i];
        j++;
        if(i+1 >= argc || strncmp(argv[i+1], ":", 1) == 0) {
            childargs[j] = NULL; // finish off argv for this child
            if ((childids[k] = fork()) == 0) {
                // this is the child
                return execv(childargs[0], childargs);
            }
            i++; // skip the ':' arg
            k++; // next child pid belongs in the next array spot
            j = 0; // reset for next child
        }
    }

    /* if any children fail, this test fails */
    int testresult = 0;

    /* we started k children, wait for them all */
    for(int i = 0; i < k; i++) {
        pid_t childpid = 0;
        int childstatus = 0;

        /* wait for a child to terminate and collect its status */
        childpid = wait(&childstatus);

        /* if wait() failed, or the child did not exit, or the child returned an error code */
        if(childpid == -1 || !WIFEXITED(childstatus) || WEXITSTATUS(childstatus) != 0) {
            testresult = -1;
        }
    }

    return testresult;
}
