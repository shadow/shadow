/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  test_sig.c: Pth test program (signal handling)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>

#include "pth.h"

static pth_t child1;
static pth_t child2;

static void *inthandler(void *_arg)
{
    sigset_t sigs;
    int sig;
    int n;

    fprintf(stderr, "inthandler: enter\n");

    /* unblock interrupt signal only here */
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    pth_sigmask(SIG_UNBLOCK, &sigs, NULL);

    /* but the user has to hit CTRL-C three times */
    for (n = 0; n < 3; n++) {
         pth_sigwait(&sigs, &sig);
         fprintf(stderr, "inthandler: SIGINT received (#%d)\n", n);
    }

    fprintf(stderr, "inthandler: cancelling child1 and child2\n");
    pth_cancel(child1);
    pth_cancel(child2);

    fprintf(stderr, "inthandler: leave\n");
    return NULL;
}

static void child_cleanup(void *arg)
{
    fprintf(stderr, "%s: running cleanup\n", (char *)arg);
    return;
}

static void *child(void *_arg)
{
    sigset_t sigs;
    char *name = (char *)_arg;
    int i;

    fprintf(stderr, "%s: enter\n", name);

    /* establish cleanup handler */
    pth_cleanup_push(child_cleanup, name);

    /* block different types of signals */
    pth_sigmask(SIG_SETMASK, NULL, &sigs);
    sigaddset(&sigs, SIGINT);
    if (strcmp(name, "child1") == 0) {
        sigaddset(&sigs, SIGUSR1);
        sigdelset(&sigs, SIGUSR2);
    }
    else {
        sigdelset(&sigs, SIGUSR1);
        sigaddset(&sigs, SIGUSR2);
    }
    pth_sigmask(SIG_SETMASK, &sigs, NULL);

    /* do a little bit of processing and show signal states */
    for (i = 0; i < 10; i++) {
        pth_sigmask(SIG_SETMASK, NULL, &sigs);
        fprintf(stderr, "%s: SIGUSR1: %s\n", name,
                sigismember(&sigs, SIGUSR1) ? "blocked" : "unblocked");
        fprintf(stderr, "%s: SIGUSR2: %s\n", name,
                sigismember(&sigs, SIGUSR2) ? "blocked" : "unblocked");
        fprintf(stderr, "%s: leave to scheduler\n", name);
        pth_sleep(1);
        fprintf(stderr, "%s: reentered from scheduler\n", name);
    }

    fprintf(stderr, "%s: leave\n", name);
    return NULL;
}

int main(int argc, char *argv[])
{
    pth_attr_t attr;
    sigset_t sigs;

    pth_init();

    fprintf(stderr, "This is TEST_SIG, a Pth test using signals.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Hit CTRL-C three times to stop this test.\n");
    fprintf(stderr, "But only after all threads were terminated.\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "main: init\n");

    /* block signals */
    pth_sigmask(SIG_SETMASK, NULL, &sigs);
    sigaddset(&sigs, SIGUSR1);
    sigaddset(&sigs, SIGUSR2);
    sigaddset(&sigs, SIGINT);
    pth_sigmask(SIG_SETMASK, &sigs, NULL);

    /* spawn childs */
    attr = pth_attr_new();
    pth_attr_set(attr, PTH_ATTR_NAME, "child1");
    child1 = pth_spawn(attr, child, (void *)"child1");
    pth_attr_set(attr, PTH_ATTR_NAME, "child2");
    child2 = pth_spawn(attr, child, (void *)"child2");
    pth_attr_set(attr, PTH_ATTR_NAME, "inthandler");
    pth_spawn(attr, inthandler, (void *)"inthandler");
    pth_attr_destroy(attr);

    /* wait until childs are finished */
    while (pth_join(NULL, NULL));

    fprintf(stderr, "main: exit\n");
    pth_kill();
    return 0;
}

