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
**  test_uctx.c: Pth test program (user-space context switching)
*/
                             /* ``Engineering does not require science.
                                Science helps a lot, but people built
                                perfectly good brick walls long before
                                they knew why cement works.''
                                                        -- Alan Cox */
#include <stdio.h>
#include <time.h>

#include "pth.h"

volatile pth_uctx_t uctx[10];

/*
 *  Test 1: master and worker "threads"
 */

volatile int worker_done[10];

static void worker(void *ctx)
{
    volatile int n = (int)ctx;
    volatile int i = 0;

    fprintf(stderr, "worker #%d: enter\n", n);
    for (i = 0; i < 100; i++) {
        fprintf(stderr, "worker #%d: working (step %d)\n", n, i);
        pth_uctx_switch(uctx[n], uctx[0]);
    }
    worker_done[n] = TRUE;
    fprintf(stderr, "worker #%d: exit\n", n);
    return;
}

static void test_working(void)
{
    volatile int i;
    volatile int todo;

    fprintf(stderr, "master: startup\n");

    fprintf(stderr, "master: create contexts\n");
    pth_uctx_create((pth_uctx_t *)&uctx[0]);
    worker_done[0] = FALSE;
    for (i = 1; i < 10; i++) {
        worker_done[i] = FALSE;
        pth_uctx_create((pth_uctx_t *)&uctx[i]);
        pth_uctx_make(uctx[i], NULL, 32*1024, NULL, worker, (void *)i, uctx[0]);
    }

    do {
        todo = 0;
        for (i = 1; i < 10; i++) {
            if (!worker_done[i]) {
                fprintf(stderr, "master: switching to worker #%d\n", i);
                pth_uctx_switch(uctx[0], uctx[i]);
                fprintf(stderr, "master: came back from worker #%d\n", i);
                todo = 1;
            }
        }
    } while (todo);

    fprintf(stderr, "master: destroy contexts\n");
    for (i = 1; i < 10; i++)
        pth_uctx_destroy(uctx[i]);
    pth_uctx_destroy(uctx[0]);

    fprintf(stderr, "master: exit\n");
    return;
}

/*
 *  Test 2: raw switching performance
 */

#define DO_SWITCHES 10000000

time_t       stat_start;
time_t       stat_end;
volatile int stat_switched;

static void dummy(void *ctx)
{
    while (1) {
        stat_switched++;
        pth_uctx_switch(uctx[1], uctx[0]);
    }
    return;
}

static void test_performance(void)
{
    volatile int i;

    pth_uctx_create((pth_uctx_t *)&uctx[0]);
    pth_uctx_create((pth_uctx_t *)&uctx[1]);
    pth_uctx_make(uctx[1], NULL, 32*1024, NULL, dummy, NULL, uctx[0]);

    fprintf(stderr, "\n");
    fprintf(stderr, "Performing %d user-space context switches... "
            "be patient!\n", DO_SWITCHES);

    stat_start = time(NULL);
    stat_switched = 0;
    for (i = 0; i < DO_SWITCHES; i++) {
        stat_switched++;
        pth_uctx_switch(uctx[0], uctx[1]);
    }
    stat_end = time(NULL);

    pth_uctx_destroy(uctx[0]);
    pth_uctx_destroy(uctx[1]);

    fprintf(stderr, "We required %d seconds for performing the test, "
            "so this means we can\n", (int)(stat_end-stat_start));
    fprintf(stderr, "perform %d user-space context switches per second "
            "on this platform.\n", DO_SWITCHES/(int)(stat_end-stat_start));
    fprintf(stderr, "\n");
    return;
}

int main(int argc, char *argv[])
{
    test_working();
    test_performance();
    return 0;
}

