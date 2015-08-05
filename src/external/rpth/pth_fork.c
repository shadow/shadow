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
**  pth_fork.c: Pth process forking support
*/
                             /* ``Every day of my life
                                  I am forced to add another
                                  name to the list of people
                                  who piss me off!''
                                            -- Calvin          */
#include "pth_p.h"

struct pth_atfork_st {
    void (*prepare)(void *);
    void (*parent)(void *);
    void (*child)(void *);
    void *arg;
};

static struct pth_atfork_st pth_atfork_list[PTH_ATFORK_MAX];
static int pth_atfork_idx = 0;

int pth_atfork_push(void (*prepare)(void *), void (*parent)(void *),
                    void (*child)(void *), void *arg)
{
    if (pth_atfork_idx > PTH_ATFORK_MAX-1)
        return pth_error(FALSE, ENOMEM);
    pth_atfork_list[pth_atfork_idx].prepare = prepare;
    pth_atfork_list[pth_atfork_idx].parent  = parent;
    pth_atfork_list[pth_atfork_idx].child   = child;
    pth_atfork_list[pth_atfork_idx].arg     = arg;
    pth_atfork_idx++;
    return TRUE;
}

int pth_atfork_pop(void)
{
    if (pth_atfork_idx <= 0)
        return FALSE;
    pth_atfork_idx--;
    return TRUE;
}

pid_t pth_fork(void)
{
    pid_t pid;
    int i;

    /* run preparation handlers in LIFO order */
    for (i = pth_atfork_idx-1; i >= 0; i--)
        if (pth_atfork_list[i].prepare != NULL)
            pth_atfork_list[i].prepare(pth_atfork_list[i].arg);

    /* fork the process */
    if ((pid = pth_sc(fork)()) == -1)
        return FALSE;

    /* handle parent and child contexts */
    if (pid != 0) {
        /* Parent: */

        /* run parent handlers in FIFO order */
        for (i = 0; i <= pth_atfork_idx-1; i++)
            if (pth_atfork_list[i].parent != NULL)
                pth_atfork_list[i].parent(pth_atfork_list[i].arg);
    }
    else {
        /* Child: */

        /* kick out all threads except for the current one and the scheduler */
        pth_scheduler_drop();

        /* run child handlers in FIFO order */
        for (i = 0; i <= pth_atfork_idx-1; i++)
            if (pth_atfork_list[i].child != NULL)
                pth_atfork_list[i].child(pth_atfork_list[i].arg);
    }
    return pid;
}

