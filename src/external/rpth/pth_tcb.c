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
**  pth_tcb.c: Pth thread control block handling
*/
                             /* Patient: Doctor, it hurts when I do this!
                                Doctor: Well, then don't do it. */
#include "pth_p.h"

#if cpp

#define PTH_TCB_NAMELEN 40

    /* thread control block */
struct pth_st {
    /* priority queue handling */
    pth_t          q_next;               /* next thread in pool                         */
    pth_t          q_prev;               /* previous thread in pool                     */
    int            q_prio;               /* (relative) priority of thread when queued   */

    /* standard thread control block ingredients */
    int            prio;                 /* base priority of thread                     */
    char           name[PTH_TCB_NAMELEN];/* name of thread (mainly for debugging)       */
    int            dispatches;           /* total number of thread dispatches           */
    pth_state_t    state;                /* current state indicator for thread          */

    /* timing */
    pth_time_t     spawned;              /* time point at which thread was spawned      */
    pth_time_t     lastran;              /* time point at which thread was last running */
    pth_time_t     running;              /* time range the thread was already running   */

    /* event handling */
    pth_event_t    events;               /* events the tread is waiting for             */

    /* per-thread signal handling */
    sigset_t       sigpending;           /* set    of pending signals                   */
    int            sigpendcnt;           /* number of pending signals                   */

    /* machine context */
    pth_mctx_t     mctx;                 /* last saved machine state of thread          */
    char          *stack;                /* pointer to thread stack                     */
    unsigned int   stacksize;            /* size of thread stack                        */
    long          *stackguard;           /* stack overflow guard                        */
    int            stackloan;            /* stack type                                  */
    void        *(*start_func)(void *);  /* start routine                               */
    void          *start_arg;            /* start argument                              */

    /* thread joining */
    int            joinable;             /* whether thread is joinable                  */
    void          *join_arg;             /* joining argument                            */

    /* per-thread specific storage */
    const void   **data_value;           /* thread specific  values                     */
    int            data_count;           /* number of stored values                     */

    /* cancellation support */
    int            cancelreq;            /* cancellation request is pending             */
    unsigned int   cancelstate;          /* cancellation state of thread                */
    pth_cleanup_t *cleanups;             /* stack of thread cleanup handlers            */

    /* mutex ring */
    pth_ring_t     mutexring;            /* ring of aquired mutex structures            */

#ifdef PTH_EX
    /* per-thread exception handling */
    ex_ctx_t       ex_ctx;               /* exception handling context                  */
#endif
};

#endif /* cpp */

intern const char *pth_state_names[] = {
    "scheduler", "new", "ready", "running", "waiting", "dead"
};

#if defined(MINSIGSTKSZ) && !defined(SIGSTKSZ)
#define SIGSTKSZ MINSIGSTKSZ
#endif
#if !defined(SIGSTKSZ)
#define SIGSTKSZ 8192
#endif

/* allocate a thread control block */
intern pth_t pth_tcb_alloc(unsigned int stacksize, void *stackaddr)
{
    pth_t t;

    if (stacksize > 0 && stacksize < SIGSTKSZ)
        stacksize = SIGSTKSZ;
    if ((t = (pth_t)malloc(sizeof(struct pth_st))) == NULL)
        return NULL;
    t->stacksize  = stacksize;
    t->stack      = NULL;
    t->stackguard = NULL;
    t->stackloan  = (stackaddr != NULL ? TRUE : FALSE);
    if (stacksize > 0) { /* stacksize == 0 means "main" thread */
        if (stackaddr != NULL)
            t->stack = (char *)(stackaddr);
        else {
            if ((t->stack = (char *)malloc(stacksize)) == NULL) {
                pth_shield { free(t); }
                return NULL;
            }
        }
#if PTH_STACKGROWTH < 0
        /* guard is at lowest address (alignment is guarrantied) */
        t->stackguard = (long *)((long)t->stack); /* double cast to avoid alignment warning */
#else
        /* guard is at highest address (be careful with alignment) */
        t->stackguard = (long *)(t->stack+(((stacksize/sizeof(long))-1)*sizeof(long)));
#endif
        *t->stackguard = 0xDEAD;
    }
    return t;
}

/* free a thread control block */
intern void pth_tcb_free(pth_t t)
{
    if (t == NULL)
        return;
    if (t->stack != NULL && !t->stackloan)
        free(t->stack);
    if (t->data_value != NULL)
        free(t->data_value);
    if (t->cleanups != NULL)
        pth_cleanup_popall(t, FALSE);
    free(t);
    return;
}

