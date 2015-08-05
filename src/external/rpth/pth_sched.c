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
**  pth_sched.c: Pth thread scheduler, the real heart of Pth
*/
                             /* ``Recursive, adj.;
                                  see Recursive.''
                                     -- Unknown   */
#include "pth_p.h"

intern pth_t        pth_main;       /* the main thread                       */
intern pth_t        pth_sched;      /* the permanent scheduler thread        */
intern pth_t        pth_current;    /* the currently running thread          */
intern pth_pqueue_t pth_NQ;         /* queue of new threads                  */
intern pth_pqueue_t pth_RQ;         /* queue of threads ready to run         */
intern pth_pqueue_t pth_WQ;         /* queue of threads waiting for an event */
intern pth_pqueue_t pth_SQ;         /* queue of suspended threads            */
intern pth_pqueue_t pth_DQ;         /* queue of terminated threads           */
intern int          pth_favournew;  /* favour new threads on startup         */
intern float        pth_loadval;    /* average scheduler load value          */

static int          pth_sigpipe[2]; /* internal signal occurrence pipe       */
static sigset_t     pth_sigpending; /* mask of pending signals               */
static sigset_t     pth_sigblock;   /* mask of signals we block in scheduler */
static sigset_t     pth_sigcatch;   /* mask of signals we have to catch      */
static sigset_t     pth_sigraised;  /* mask of raised signals                */

static pth_time_t   pth_loadticknext;
static pth_time_t   pth_loadtickgap = PTH_TIME(1,0);

/* initialize the scheduler ingredients */
intern int pth_scheduler_init(void)
{
    /* create the internal signal pipe */
    if (pipe(pth_sigpipe) == -1)
        return pth_error(FALSE, errno);
    if (pth_fdmode(pth_sigpipe[0], PTH_FDMODE_NONBLOCK) == PTH_FDMODE_ERROR)
        return pth_error(FALSE, errno);
    if (pth_fdmode(pth_sigpipe[1], PTH_FDMODE_NONBLOCK) == PTH_FDMODE_ERROR)
        return pth_error(FALSE, errno);

    /* initialize the essential threads */
    pth_sched   = NULL;
    pth_current = NULL;

    /* initalize the thread queues */
    pth_pqueue_init(&pth_NQ);
    pth_pqueue_init(&pth_RQ);
    pth_pqueue_init(&pth_WQ);
    pth_pqueue_init(&pth_SQ);
    pth_pqueue_init(&pth_DQ);

    /* initialize scheduling hints */
    pth_favournew = 1; /* the default is the original behaviour */

    /* initialize load support */
    pth_loadval = 1.0;
    pth_time_set(&pth_loadticknext, PTH_TIME_NOW);

    return TRUE;
}

/* drop all threads (except for the currently active one) */
intern void pth_scheduler_drop(void)
{
    pth_t t;

    /* clear the new queue */
    while ((t = pth_pqueue_delmax(&pth_NQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_NQ);

    /* clear the ready queue */
    while ((t = pth_pqueue_delmax(&pth_RQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_RQ);

    /* clear the waiting queue */
    while ((t = pth_pqueue_delmax(&pth_WQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_WQ);

    /* clear the suspend queue */
    while ((t = pth_pqueue_delmax(&pth_SQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_SQ);

    /* clear the dead queue */
    while ((t = pth_pqueue_delmax(&pth_DQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_DQ);
    return;
}

/* kill the scheduler ingredients */
intern void pth_scheduler_kill(void)
{
    /* drop all threads */
    pth_scheduler_drop();

    /* remove the internal signal pipe */
    close(pth_sigpipe[0]);
    close(pth_sigpipe[1]);
    return;
}

/*
 * Update the average scheduler load.
 *
 * This is called on every context switch, but we have to adjust the
 * average load value every second, only. If we're called more than
 * once per second we handle this by just calculating anything once
 * and then do NOPs until the next ticks is over. If the scheduler
 * waited for more than once second (or a thread CPU burst lasted for
 * more than once second) we simulate the missing calculations. That's
 * no problem because we can assume that the number of ready threads
 * then wasn't changed dramatically (or more context switched would have
 * been occurred and we would have been given more chances to operate).
 * The actual average load is calculated through an exponential average
 * formula.
 */
#define pth_scheduler_load(now) \
    if (pth_time_cmp((now), &pth_loadticknext) >= 0) { \
        pth_time_t ttmp; \
        int numready; \
        numready = pth_pqueue_elements(&pth_RQ); \
        pth_time_set(&ttmp, (now)); \
        do { \
            pth_loadval = (numready*0.25) + (pth_loadval*0.75); \
            pth_time_sub(&ttmp, &pth_loadtickgap); \
        } while (pth_time_cmp(&ttmp, &pth_loadticknext) >= 0); \
        pth_time_set(&pth_loadticknext, (now)); \
        pth_time_add(&pth_loadticknext, &pth_loadtickgap); \
    }

/* the heart of this library: the thread scheduler */
intern void *pth_scheduler(void *dummy)
{
    sigset_t sigs;
    pth_time_t running;
    pth_time_t snapshot;
    struct sigaction sa;
    sigset_t ss;
    int sig;
    pth_t t;

    /*
     * bootstrapping
     */
    pth_debug1("pth_scheduler: bootstrapping");

    /* mark this thread as the special scheduler thread */
    pth_sched->state = PTH_STATE_SCHEDULER;

    /* block all signals in the scheduler thread */
    sigfillset(&sigs);
    pth_sc(sigprocmask)(SIG_SETMASK, &sigs, NULL);

    /* initialize the snapshot time for bootstrapping the loop */
    pth_time_set(&snapshot, PTH_TIME_NOW);

    /*
     * endless scheduler loop
     */
    for (;;) {
        /*
         * Move threads from new queue to ready queue and optionally
         * give them maximum priority so they start immediately.
         */
        while ((t = pth_pqueue_tail(&pth_NQ)) != NULL) {
            pth_pqueue_delete(&pth_NQ, t);
            t->state = PTH_STATE_READY;
            if (pth_favournew)
                pth_pqueue_insert(&pth_RQ, pth_pqueue_favorite_prio(&pth_RQ), t);
            else
                pth_pqueue_insert(&pth_RQ, PTH_PRIO_STD, t);
            pth_debug2("pth_scheduler: new thread \"%s\" moved to top of ready queue", t->name);
        }

        /*
         * Update average scheduler load
         */
        pth_scheduler_load(&snapshot);

        /*
         * Find next thread in ready queue
         */
        pth_current = pth_pqueue_delmax(&pth_RQ);
        if (pth_current == NULL) {
            fprintf(stderr, "**Pth** SCHEDULER INTERNAL ERROR: "
                            "no more thread(s) available to schedule!?!?\n");
            abort();
        }
        pth_debug4("pth_scheduler: thread \"%s\" selected (prio=%d, qprio=%d)",
                   pth_current->name, pth_current->prio, pth_current->q_prio);

        /*
         * Raise additionally thread-specific signals
         * (they are delivered when we switch the context)
         *
         * Situation is ('#' = signal pending):
         *     process pending (pth_sigpending):         ----####
         *     thread pending (pth_current->sigpending): --##--##
         * Result has to be:
         *     process new pending:                      --######
         */
        if (pth_current->sigpendcnt > 0) {
            sigpending(&pth_sigpending);
            for (sig = 1; sig < PTH_NSIG; sig++)
                if (sigismember(&pth_current->sigpending, sig))
                    if (!sigismember(&pth_sigpending, sig))
                        kill(getpid(), sig);
        }

        /*
         * Set running start time for new thread
         * and perform a context switch to it
         */
        pth_debug3("pth_scheduler: switching to thread 0x%lx (\"%s\")",
                   (unsigned long)pth_current, pth_current->name);

        /* update thread times */
        pth_time_set(&pth_current->lastran, PTH_TIME_NOW);

        /* update scheduler times */
        pth_time_set(&running, &pth_current->lastran);
        pth_time_sub(&running, &snapshot);
        pth_time_add(&pth_sched->running, &running);

        /* ** ENTERING THREAD ** - by switching the machine context */
        pth_current->dispatches++;
        pth_mctx_switch(&pth_sched->mctx, &pth_current->mctx);

        /* update scheduler times */
        pth_time_set(&snapshot, PTH_TIME_NOW);
        pth_debug3("pth_scheduler: cameback from thread 0x%lx (\"%s\")",
                   (unsigned long)pth_current, pth_current->name);

        /*
         * Calculate and update the time the previous thread was running
         */
        pth_time_set(&running, &snapshot);
        pth_time_sub(&running, &pth_current->lastran);
        pth_time_add(&pth_current->running, &running);
        pth_debug3("pth_scheduler: thread \"%s\" ran %.6f",
                   pth_current->name, pth_time_t2d(&running));

        /*
         * Remove still pending thread-specific signals
         * (they are re-delivered next time)
         *
         * Situation is ('#' = signal pending):
         *     thread old pending (pth_current->sigpending): --##--##
         *     process old pending (pth_sigpending):         ----####
         *     process still pending (sigstillpending):      ---#-#-#
         * Result has to be:
         *     process new pending:                          -----#-#
         *     thread new pending (pth_current->sigpending): ---#---#
         */
        if (pth_current->sigpendcnt > 0) {
            sigset_t sigstillpending;
            sigpending(&sigstillpending);
            for (sig = 1; sig < PTH_NSIG; sig++) {
                if (sigismember(&pth_current->sigpending, sig)) {
                    if (!sigismember(&sigstillpending, sig)) {
                        /* thread (and perhaps also process) signal delivered */
                        sigdelset(&pth_current->sigpending, sig);
                        pth_current->sigpendcnt--;
                    }
                    else if (!sigismember(&pth_sigpending, sig)) {
                        /* thread signal not delivered */
                        pth_util_sigdelete(sig);
                    }
                }
            }
        }

        /*
         * Check for stack overflow
         */
        if (pth_current->stackguard != NULL) {
            if (*pth_current->stackguard != 0xDEAD) {
                pth_debug3("pth_scheduler: stack overflow detected for thread 0x%lx (\"%s\")",
                           (unsigned long)pth_current, pth_current->name);
                /*
                 * if the application doesn't catch SIGSEGVs, we terminate
                 * manually with a SIGSEGV now, but output a reasonable message.
                 */
                if (sigaction(SIGSEGV, NULL, &sa) == 0) {
                    if (sa.sa_handler == SIG_DFL) {
                        fprintf(stderr, "**Pth** STACK OVERFLOW: thread pid_t=0x%lx, name=\"%s\"\n",
                                (unsigned long)pth_current, pth_current->name);
                        kill(getpid(), SIGSEGV);
                        sigfillset(&ss);
                        sigdelset(&ss, SIGSEGV);
                        sigsuspend(&ss);
                        abort();
                    }
                }
                /*
                 * else we terminate the thread only and send us a SIGSEGV
                 * which allows the application to handle the situation...
                 */
                pth_current->join_arg = (void *)0xDEAD;
                pth_current->state = PTH_STATE_DEAD;
                kill(getpid(), SIGSEGV);
            }
        }

        /*
         * If previous thread is now marked as dead, kick it out
         */
        if (pth_current->state == PTH_STATE_DEAD) {
            pth_debug2("pth_scheduler: marking thread \"%s\" as dead", pth_current->name);
            if (!pth_current->joinable)
                pth_tcb_free(pth_current);
            else
                pth_pqueue_insert(&pth_DQ, PTH_PRIO_STD, pth_current);
            pth_current = NULL;
        }

        /*
         * If thread wants to wait for an event
         * move it to waiting queue now
         */
        if (pth_current != NULL && pth_current->state == PTH_STATE_WAITING) {
            pth_debug2("pth_scheduler: moving thread \"%s\" to waiting queue",
                       pth_current->name);
            pth_pqueue_insert(&pth_WQ, pth_current->prio, pth_current);
            pth_current = NULL;
        }

        /*
         * migrate old treads in ready queue into higher
         * priorities to avoid starvation and insert last running
         * thread back into this queue, too.
         */
        pth_pqueue_increase(&pth_RQ);
        if (pth_current != NULL)
            pth_pqueue_insert(&pth_RQ, pth_current->prio, pth_current);

        /*
         * Manage the events in the waiting queue, i.e. decide whether their
         * events occurred and move them to the ready queue. But wait only if
         * we have already no new or ready threads.
         */
        if (   pth_pqueue_elements(&pth_RQ) == 0
            && pth_pqueue_elements(&pth_NQ) == 0)
            /* still no NEW or READY threads, so we have to wait for new work */
            pth_sched_eventmanager(&snapshot, FALSE /* wait */);
        else
            /* already NEW or READY threads exists, so just poll for even more work */
            pth_sched_eventmanager(&snapshot, TRUE  /* poll */);
    }

    /* NOTREACHED */
    return NULL;
}

/*
 * Look whether some events already occurred (or failed) and move
 * corresponding threads from waiting queue back to ready queue.
 */
intern void pth_sched_eventmanager(pth_time_t *now, int dopoll)
{
    pth_t nexttimer_thread;
    pth_event_t nexttimer_ev;
    pth_time_t nexttimer_value;
    pth_event_t evh;
    pth_event_t ev;
    pth_t t;
    pth_t tlast;
    int this_occurred;
    int any_occurred;
    fd_set rfds;
    fd_set wfds;
    fd_set efds;
    struct timeval delay;
    struct timeval *pdelay;
    sigset_t oss;
    struct sigaction sa;
    struct sigaction osa[1+PTH_NSIG];
    char minibuf[128];
    int loop_repeat;
    int fdmax;
    int rc;
    int sig;
    int n;

    pth_debug2("pth_sched_eventmanager: enter in %s mode",
               dopoll ? "polling" : "waiting");

    /* entry point for internal looping in event handling */
    loop_entry:
    loop_repeat = FALSE;

    /* initialize fd sets */
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    fdmax = -1;

    /* initialize signal status */
    sigpending(&pth_sigpending);
    sigfillset(&pth_sigblock);
    sigemptyset(&pth_sigcatch);
    sigemptyset(&pth_sigraised);

    /* initialize next timer */
    pth_time_set(&nexttimer_value, PTH_TIME_ZERO);
    nexttimer_thread = NULL;
    nexttimer_ev = NULL;

    /* for all threads in the waiting queue... */
    any_occurred = FALSE;
    for (t = pth_pqueue_head(&pth_WQ); t != NULL;
         t = pth_pqueue_walk(&pth_WQ, t, PTH_WALK_NEXT)) {

        /* determine signals we block */
        for (sig = 1; sig < PTH_NSIG; sig++)
            if (!sigismember(&(t->mctx.sigs), sig))
                sigdelset(&pth_sigblock, sig);

        /* cancellation support */
        if (t->cancelreq == TRUE)
            any_occurred = TRUE;

        /* ... and all their events... */
        if (t->events == NULL)
            continue;
        /* ...check whether events occurred */
        ev = evh = t->events;
        do {
            if (ev->ev_status == PTH_STATUS_PENDING) {
                this_occurred = FALSE;

                /* Filedescriptor I/O */
                if (ev->ev_type == PTH_EVENT_FD) {
                    /* filedescriptors are checked later all at once.
                       Here we only assemble them in the fd sets */
                    if (ev->ev_goal & PTH_UNTIL_FD_READABLE)
                        FD_SET(ev->ev_args.FD.fd, &rfds);
                    if (ev->ev_goal & PTH_UNTIL_FD_WRITEABLE)
                        FD_SET(ev->ev_args.FD.fd, &wfds);
                    if (ev->ev_goal & PTH_UNTIL_FD_EXCEPTION)
                        FD_SET(ev->ev_args.FD.fd, &efds);
                    if (fdmax < ev->ev_args.FD.fd)
                        fdmax = ev->ev_args.FD.fd;
                }
                /* Filedescriptor Set Select I/O */
                else if (ev->ev_type == PTH_EVENT_SELECT) {
                    /* filedescriptors are checked later all at once.
                       Here we only merge the fd sets. */
                    pth_util_fds_merge(ev->ev_args.SELECT.nfd,
                                       ev->ev_args.SELECT.rfds, &rfds,
                                       ev->ev_args.SELECT.wfds, &wfds,
                                       ev->ev_args.SELECT.efds, &efds);
                    if (fdmax < ev->ev_args.SELECT.nfd-1)
                        fdmax = ev->ev_args.SELECT.nfd-1;
                }
                /* Signal Set */
                else if (ev->ev_type == PTH_EVENT_SIGS) {
                    for (sig = 1; sig < PTH_NSIG; sig++) {
                        if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
                            /* thread signal handling */
                            if (sigismember(&t->sigpending, sig)) {
                                *(ev->ev_args.SIGS.sig) = sig;
                                sigdelset(&t->sigpending, sig);
                                t->sigpendcnt--;
                                this_occurred = TRUE;
                            }
                            /* process signal handling */
                            if (sigismember(&pth_sigpending, sig)) {
                                if (ev->ev_args.SIGS.sig != NULL)
                                    *(ev->ev_args.SIGS.sig) = sig;
                                pth_util_sigdelete(sig);
                                sigdelset(&pth_sigpending, sig);
                                this_occurred = TRUE;
                            }
                            else {
                                sigdelset(&pth_sigblock, sig);
                                sigaddset(&pth_sigcatch, sig);
                            }
                        }
                    }
                }
                /* Timer */
                else if (ev->ev_type == PTH_EVENT_TIME) {
                    if (pth_time_cmp(&(ev->ev_args.TIME.tv), now) < 0)
                        this_occurred = TRUE;
                    else {
                        /* remember the timer which will be elapsed next */
                        if ((nexttimer_thread == NULL && nexttimer_ev == NULL) ||
                            pth_time_cmp(&(ev->ev_args.TIME.tv), &nexttimer_value) < 0) {
                            nexttimer_thread = t;
                            nexttimer_ev = ev;
                            pth_time_set(&nexttimer_value, &(ev->ev_args.TIME.tv));
                        }
                    }
                }
                /* Message Port Arrivals */
                else if (ev->ev_type == PTH_EVENT_MSG) {
                    if (pth_ring_elements(&(ev->ev_args.MSG.mp->mp_queue)) > 0)
                        this_occurred = TRUE;
                }
                /* Mutex Release */
                else if (ev->ev_type == PTH_EVENT_MUTEX) {
                    if (!(ev->ev_args.MUTEX.mutex->mx_state & PTH_MUTEX_LOCKED))
                        this_occurred = TRUE;
                }
                /* Condition Variable Signal */
                else if (ev->ev_type == PTH_EVENT_COND) {
                    if (ev->ev_args.COND.cond->cn_state & PTH_COND_SIGNALED) {
                        if (ev->ev_args.COND.cond->cn_state & PTH_COND_BROADCAST)
                            this_occurred = TRUE;
                        else {
                            if (!(ev->ev_args.COND.cond->cn_state & PTH_COND_HANDLED)) {
                                ev->ev_args.COND.cond->cn_state |= PTH_COND_HANDLED;
                                this_occurred = TRUE;
                            }
                        }
                    }
                }
                /* Thread Termination */
                else if (ev->ev_type == PTH_EVENT_TID) {
                    if (   (   ev->ev_args.TID.tid == NULL
                            && pth_pqueue_elements(&pth_DQ) > 0)
                        || (   ev->ev_args.TID.tid != NULL
                            && ev->ev_args.TID.tid->state == ev->ev_goal))
                        this_occurred = TRUE;
                }
                /* Custom Event Function */
                else if (ev->ev_type == PTH_EVENT_FUNC) {
                    if (ev->ev_args.FUNC.func(ev->ev_args.FUNC.arg))
                        this_occurred = TRUE;
                    else {
                        pth_time_t tv;
                        pth_time_set(&tv, now);
                        pth_time_add(&tv, &(ev->ev_args.FUNC.tv));
                        if ((nexttimer_thread == NULL && nexttimer_ev == NULL) ||
                            pth_time_cmp(&tv, &nexttimer_value) < 0) {
                            nexttimer_thread = t;
                            nexttimer_ev = ev;
                            pth_time_set(&nexttimer_value, &tv);
                        }
                    }
                }

                /* tag event if it has occurred */
                if (this_occurred) {
                    pth_debug2("pth_sched_eventmanager: [non-I/O] event occurred for thread \"%s\"", t->name);
                    ev->ev_status = PTH_STATUS_OCCURRED;
                    any_occurred = TRUE;
                }
            }
        } while ((ev = ev->ev_next) != evh);
    }
    if (any_occurred)
        dopoll = TRUE;

    /* now decide how to poll for fd I/O and timers */
    if (dopoll) {
        /* do a polling with immediate timeout,
           i.e. check the fd sets only without blocking */
        pth_time_set(&delay, PTH_TIME_ZERO);
        pdelay = &delay;
    }
    else if (nexttimer_ev != NULL) {
        /* do a polling with a timeout set to the next timer,
           i.e. wait for the fd sets or the next timer */
        pth_time_set(&delay, &nexttimer_value);
        pth_time_sub(&delay, now);
        pdelay = &delay;
    }
    else {
        /* do a polling without a timeout,
           i.e. wait for the fd sets only with blocking */
        pdelay = NULL;
    }

    /* clear pipe and let select() wait for the read-part of the pipe */
    while (pth_sc(read)(pth_sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;
    FD_SET(pth_sigpipe[0], &rfds);
    if (fdmax < pth_sigpipe[0])
        fdmax = pth_sigpipe[0];

    /* replace signal actions for signals we've to catch for events */
    for (sig = 1; sig < PTH_NSIG; sig++) {
        if (sigismember(&pth_sigcatch, sig)) {
            sa.sa_handler = pth_sched_eventmanager_sighandler;
            sigfillset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(sig, &sa, &osa[sig]);
        }
    }

    /* allow some signals to be delivered: Either to our
       catching handler or directly to the configured
       handler for signals not catched by events */
    pth_sc(sigprocmask)(SIG_SETMASK, &pth_sigblock, &oss);

    /* now do the polling for filedescriptor I/O and timers
       WHEN THE SCHEDULER SLEEPS AT ALL, THEN HERE!! */
    rc = -1;
    if (!(dopoll && fdmax == -1))
        while ((rc = pth_sc(select)(fdmax+1, &rfds, &wfds, &efds, pdelay)) < 0
               && errno == EINTR) ;

    /* restore signal mask and actions and handle signals */
    pth_sc(sigprocmask)(SIG_SETMASK, &oss, NULL);
    for (sig = 1; sig < PTH_NSIG; sig++)
        if (sigismember(&pth_sigcatch, sig))
            sigaction(sig, &osa[sig], NULL);

    /* if the timer elapsed, handle it */
    if (!dopoll && rc == 0 && nexttimer_ev != NULL) {
        if (nexttimer_ev->ev_type == PTH_EVENT_FUNC) {
            /* it was an implicit timer event for a function event,
               so repeat the event handling for rechecking the function */
            loop_repeat = TRUE;
        }
        else {
            /* it was an explicit timer event, standing for its own */
            pth_debug2("pth_sched_eventmanager: [timeout] event occurred for thread \"%s\"",
                       nexttimer_thread->name);
            nexttimer_ev->ev_status = PTH_STATUS_OCCURRED;
        }
    }

    /* if the internal signal pipe was used, adjust the select() results */
    if (!dopoll && rc > 0 && FD_ISSET(pth_sigpipe[0], &rfds)) {
        FD_CLR(pth_sigpipe[0], &rfds);
        rc--;
    }

    /* if an error occurred, avoid confusion in the cleanup loop */
    if (rc <= 0) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
    }

    /* now comes the final cleanup loop where we've to
       do two jobs: first we've to do the late handling of the fd I/O events and
       additionally if a thread has one occurred event, we move it from the
       waiting queue to the ready queue */

    /* for all threads in the waiting queue... */
    t = pth_pqueue_head(&pth_WQ);
    while (t != NULL) {

        /* do the late handling of the fd I/O and signal
           events in the waiting event ring */
        any_occurred = FALSE;
        if (t->events != NULL) {
            ev = evh = t->events;
            do {
                /*
                 * Late handling for still not occured events
                 */
                if (ev->ev_status == PTH_STATUS_PENDING) {
                    /* Filedescriptor I/O */
                    if (ev->ev_type == PTH_EVENT_FD) {
                        if (   (   ev->ev_goal & PTH_UNTIL_FD_READABLE
                                && FD_ISSET(ev->ev_args.FD.fd, &rfds))
                            || (   ev->ev_goal & PTH_UNTIL_FD_WRITEABLE
                                && FD_ISSET(ev->ev_args.FD.fd, &wfds))
                            || (   ev->ev_goal & PTH_UNTIL_FD_EXCEPTION
                                && FD_ISSET(ev->ev_args.FD.fd, &efds)) ) {
                            pth_debug2("pth_sched_eventmanager: "
                                       "[I/O] event occurred for thread \"%s\"", t->name);
                            ev->ev_status = PTH_STATUS_OCCURRED;
                        }
                        else if (rc < 0) {
                            /* re-check particular filedescriptor */
                            int rc2;
                            if (ev->ev_goal & PTH_UNTIL_FD_READABLE)
                                FD_SET(ev->ev_args.FD.fd, &rfds);
                            if (ev->ev_goal & PTH_UNTIL_FD_WRITEABLE)
                                FD_SET(ev->ev_args.FD.fd, &wfds);
                            if (ev->ev_goal & PTH_UNTIL_FD_EXCEPTION)
                                FD_SET(ev->ev_args.FD.fd, &efds);
                            pth_time_set(&delay, PTH_TIME_ZERO);
                            while ((rc2 = pth_sc(select)(ev->ev_args.FD.fd+1, &rfds, &wfds, &efds, &delay)) < 0
                                   && errno == EINTR) ;
                            if (rc2 > 0) {
                                /* cleanup afterwards for next iteration */
                                FD_CLR(ev->ev_args.FD.fd, &rfds);
                                FD_CLR(ev->ev_args.FD.fd, &wfds);
                                FD_CLR(ev->ev_args.FD.fd, &efds);
                            } else if (rc2 < 0) {
                                /* cleanup afterwards for next iteration */
                                FD_ZERO(&rfds);
                                FD_ZERO(&wfds);
                                FD_ZERO(&efds);
                                ev->ev_status = PTH_STATUS_FAILED;
                                pth_debug2("pth_sched_eventmanager: "
                                           "[I/O] event failed for thread \"%s\"", t->name);
                            }
                        }
                    }
                    /* Filedescriptor Set I/O */
                    else if (ev->ev_type == PTH_EVENT_SELECT) {
                        if (pth_util_fds_test(ev->ev_args.SELECT.nfd,
                                              ev->ev_args.SELECT.rfds, &rfds,
                                              ev->ev_args.SELECT.wfds, &wfds,
                                              ev->ev_args.SELECT.efds, &efds)) {
                            n = pth_util_fds_select(ev->ev_args.SELECT.nfd,
                                                    ev->ev_args.SELECT.rfds, &rfds,
                                                    ev->ev_args.SELECT.wfds, &wfds,
                                                    ev->ev_args.SELECT.efds, &efds);
                            if (ev->ev_args.SELECT.n != NULL)
                                *(ev->ev_args.SELECT.n) = n;
                            ev->ev_status = PTH_STATUS_OCCURRED;
                            pth_debug2("pth_sched_eventmanager: "
                                       "[I/O] event occurred for thread \"%s\"", t->name);
                        }
                        else if (rc < 0) {
                            /* re-check particular filedescriptor set */
                            int rc2;
                            fd_set *prfds = NULL;
                            fd_set *pwfds = NULL;
                            fd_set *pefds = NULL;
                            fd_set trfds;
                            fd_set twfds;
                            fd_set tefds;
                            if (ev->ev_args.SELECT.rfds) {
                                memcpy(&trfds, ev->ev_args.SELECT.rfds, sizeof(rfds));
                                prfds = &trfds;
                            }
                            if (ev->ev_args.SELECT.wfds) {
                                memcpy(&twfds, ev->ev_args.SELECT.wfds, sizeof(wfds));
                                pwfds = &twfds;
                            }
                            if (ev->ev_args.SELECT.efds) {
                                memcpy(&tefds, ev->ev_args.SELECT.efds, sizeof(efds));
                                pefds = &tefds;
                            }
                            pth_time_set(&delay, PTH_TIME_ZERO);
                            while ((rc2 = pth_sc(select)(ev->ev_args.SELECT.nfd+1, prfds, pwfds, pefds, &delay)) < 0
                                   && errno == EINTR) ;
                            if (rc2 < 0) {
                                ev->ev_status = PTH_STATUS_FAILED;
                                pth_debug2("pth_sched_eventmanager: "
                                           "[I/O] event failed for thread \"%s\"", t->name);
                            }
                        }
                    }
                    /* Signal Set */
                    else if (ev->ev_type == PTH_EVENT_SIGS) {
                        for (sig = 1; sig < PTH_NSIG; sig++) {
                            if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
                                if (sigismember(&pth_sigraised, sig)) {
                                    if (ev->ev_args.SIGS.sig != NULL)
                                        *(ev->ev_args.SIGS.sig) = sig;
                                    pth_debug2("pth_sched_eventmanager: "
                                               "[signal] event occurred for thread \"%s\"", t->name);
                                    sigdelset(&pth_sigraised, sig);
                                    ev->ev_status = PTH_STATUS_OCCURRED;
                                }
                            }
                        }
                    }
                }
                /*
                 * post-processing for already occured events
                 */
                else {
                    /* Condition Variable Signal */
                    if (ev->ev_type == PTH_EVENT_COND) {
                        /* clean signal */
                        if (ev->ev_args.COND.cond->cn_state & PTH_COND_SIGNALED) {
                            ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_SIGNALED);
                            ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_BROADCAST);
                            ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_HANDLED);
                        }
                    }
                }

                /* local to global mapping */
                if (ev->ev_status != PTH_STATUS_PENDING)
                    any_occurred = TRUE;
            } while ((ev = ev->ev_next) != evh);
        }

        /* cancellation support */
        if (t->cancelreq == TRUE) {
            pth_debug2("pth_sched_eventmanager: cancellation request pending for thread \"%s\"", t->name);
            any_occurred = TRUE;
        }

        /* walk to next thread in waiting queue */
        tlast = t;
        t = pth_pqueue_walk(&pth_WQ, t, PTH_WALK_NEXT);

        /*
         * move last thread to ready queue if any events occurred for it.
         * we insert it with a slightly increased queue priority to it a
         * better chance to immediately get scheduled, else the last running
         * thread might immediately get again the CPU which is usually not
         * what we want, because we oven use pth_yield() calls to give others
         * a chance.
         */
        if (any_occurred) {
            pth_pqueue_delete(&pth_WQ, tlast);
            tlast->state = PTH_STATE_READY;
            pth_pqueue_insert(&pth_RQ, tlast->prio+1, tlast);
            pth_debug2("pth_sched_eventmanager: thread \"%s\" moved from waiting "
                       "to ready queue", tlast->name);
        }
    }

    /* perhaps we have to internally loop... */
    if (loop_repeat) {
        pth_time_set(now, PTH_TIME_NOW);
        goto loop_entry;
    }

    pth_debug1("pth_sched_eventmanager: leaving");
    return;
}

intern void pth_sched_eventmanager_sighandler(int sig)
{
    char c;

    /* remember raised signal */
    sigaddset(&pth_sigraised, sig);

    /* write signal to signal pipe in order to awake the select() */
    c = (int)sig;
    pth_sc(write)(pth_sigpipe[1], &c, sizeof(char));
    return;
}

