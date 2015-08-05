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
**  test_philo.c: Pth test application showing the 5 philosopher problem
*/
                             /* ``It's not enough to be a great programmer;
                                  you have to find a great problem.''
                                                -- Charles Simonyi  */

/*
 *  Reference: E.W. Dijkstra,
 *             ``Hierarchical Ordering of Sequential Processes'',
 *             Acta Informatica 1, 1971, 115-138.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

#include "pth.h"

#include "test_common.h"

#define PHILNUM 5

typedef enum {
    thinking,
    hungry,
    eating
} philstat;

static const char *philstatstr[3] = {
    "thinking",
    "hungry  ",
    "EATING  "
};

typedef struct tablestruct {
    pth_t       tid[PHILNUM];
    int         self[PHILNUM];
    pth_mutex_t mutex;
    pth_cond_t  condition[PHILNUM];
    philstat    status[PHILNUM];
} table;

static table *tab;

static void printstate(void)
{
    int i;

    for (i = 0; i < PHILNUM; i++)
        printf("| %s ", philstatstr[(int)(tab->status)[i]]);
    printf("|\n");
    return;
}

static int test(unsigned int i)
{
    if (   ((tab->status)[i] == hungry)
        && ((tab->status)[(i + 1) % PHILNUM] != eating)
        && ((tab->status)[(i - 1 + PHILNUM) % PHILNUM] != eating)) {
        (tab->status)[i] = eating;
        pth_cond_notify(&((tab->condition)[i]), FALSE);
        return TRUE;
    }
    return FALSE;
}

static void pickup(unsigned int k)
{
    pth_mutex_acquire(&(tab->mutex), FALSE, NULL);
    (tab->status)[k] = hungry;
    printstate();
    if (!test(k))
        pth_cond_await(&((tab->condition)[k]), &(tab->mutex), NULL);
    printstate();
    pth_mutex_release(&(tab->mutex));
    return;
}

static void putdown(unsigned int k)
{
    pth_mutex_acquire(&(tab->mutex), FALSE, NULL);
    (tab->status)[k] = thinking;
    printstate();
    test((k + 1) % PHILNUM);
    test((k - 1 + PHILNUM) % PHILNUM);
    pth_mutex_release(&(tab->mutex));
    return;
}

static void *philosopher(void *_who)
{
    unsigned int *who = (unsigned int *)_who;

    /* For simplicity, all philosophers eat for the same amount of time
       and think for a time that is simply related to their position at
       the table. The parameter who identifies the philosopher: 0,1,2,.. */
    for (;;) {
        pth_sleep((*who) + 1);
        pickup((*who));
        pth_sleep(1);
        putdown((*who));
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int i;
    sigset_t ss;
    int sig;
    pth_event_t ev;

    /* initialize Pth library */
    pth_init();

    /* display test program header */
    printf("This is TEST_PHILO, a Pth test showing the Five Dining Philosophers\n");
    printf("\n");
    printf("This is a demonstration showing the famous concurrency problem of the\n");
    printf("Five Dining Philosophers as analysed 1965 by E.W.Dijkstra:\n");
    printf("\n");
    printf("Five philosophers are sitting around a round table, each with a bowl of\n");
    printf("Chinese food in front of him. Between periods of talking they may start\n");
    printf("eating whenever they want to, with their bowls being filled frequently.\n");
    printf("But there are only five chopsticks available, one each to the left of\n");
    printf("each bowl - and for eating Chinese food one needs two chopsticks. When\n");
    printf("a philosopher wants to start eating, he must pick up the chopstick to\n");
    printf("the left of his bowl and the chopstick to the right of his bowl. He\n");
    printf("may find, however, that either one (or even both) of the chopsticks is\n");
    printf("unavailable as it is being used by another philosopher sitting on his\n");
    printf("right or left, so he has to wait.\n");
    printf("\n");
    printf("This situation shows classical contention under concurrency (the\n");
    printf("philosophers want to grab the chopsticks) and the possibility of a\n");
    printf("deadlock (all philosophers wait that the chopstick to their left becomes\n");
    printf("available).\n");
    printf("\n");
    printf("The demonstration runs max. 60 seconds. To stop before, press CTRL-C.\n");
    printf("\n");
    printf("+----P1----+----P2----+----P3----+----P4----+----P5----+\n");

    /* initialize the control table */
    tab = (table *)malloc(sizeof(table));
    if (!pth_mutex_init(&(tab->mutex))) {
        perror("pth_mutex_init");
        exit(1);
    }
    for (i = 0; i < PHILNUM; i++) {
        (tab->self)[i] = i;
        (tab->status)[i] = thinking;
        if (!pth_cond_init(&((tab->condition)[i]))) {
            perror("pth_cond_init");
            exit(1);
        }
    }

    /* spawn the philosopher threads */
    for (i = 0; i < PHILNUM; i++) {
        if (((tab->tid)[i] =
              pth_spawn(PTH_ATTR_DEFAULT, philosopher,
                        &((tab->self)[i]))) == NULL) {
            perror("pth_spawn");
            exit(1);
        }
    }

    /* wait until 60 seconds have elapsed or CTRL-C was pressed */
    sigemptyset(&ss);
    sigaddset(&ss, SIGINT);
    ev = pth_event(PTH_EVENT_TIME, pth_timeout(60,0));
    pth_sigwait_ev(&ss, &sig, ev);
    pth_event_free(ev, PTH_FREE_ALL);

    /* cancel and join the philosopher threads */
    for (i = 0; i < PHILNUM; i++)
        pth_cancel((tab->tid)[i]);
    while (pth_join(NULL, NULL));

    /* finish display */
    printf("+----------+----------+----------+----------+----------+\n");

    /* free the control table */
    free(tab);

    /* shutdown Pth library */
    pth_kill();

    return 0;
}

