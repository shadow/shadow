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

/* initialize the scheduler ingredients */
intern int pth_scheduler_init(void)
{
    /* create the internal signal pipe */
    if (pipe(pth_gctx_get()->pth_sigpipe) == -1)
        return pth_error(FALSE, errno);
    if (pth_fdmode(pth_gctx_get()->pth_sigpipe[0], PTH_FDMODE_NONBLOCK) == PTH_FDMODE_ERROR)
        return pth_error(FALSE, errno);
    if (pth_fdmode(pth_gctx_get()->pth_sigpipe[1], PTH_FDMODE_NONBLOCK) == PTH_FDMODE_ERROR)
        return pth_error(FALSE, errno);

    /* initialize the essential threads */
    pth_gctx_get()->pth_sched   = NULL;
    pth_gctx_get()->pth_current = NULL;

    /* initalize the thread queues */
    pth_pqueue_init(&pth_gctx_get()->pth_NQ);
    pth_pqueue_init(&pth_gctx_get()->pth_RQ);
    pth_pqueue_init(&pth_gctx_get()->pth_WQ);
    pth_pqueue_init(&pth_gctx_get()->pth_SQ);
    pth_pqueue_init(&pth_gctx_get()->pth_DQ);

    /* initialize scheduling hints */
    pth_gctx_get()->pth_favournew = 1; /* the default is the original behaviour */

    /* initialize load support */
    pth_gctx_get()->pth_loadval = 1.0;
    pth_time_set(&pth_gctx_get()->pth_loadticknext, PTH_TIME_NOW);

    return TRUE;
}

/* drop all threads (except for the currently active one) */
intern void pth_scheduler_drop(void)
{
    pth_t t;

    /* clear the new queue */
    while ((t = pth_pqueue_delmax(&pth_gctx_get()->pth_NQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_gctx_get()->pth_NQ);

    /* clear the ready queue */
    while ((t = pth_pqueue_delmax(&pth_gctx_get()->pth_RQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_gctx_get()->pth_RQ);

    /* clear the waiting queue */
    while ((t = pth_pqueue_delmax(&pth_gctx_get()->pth_WQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_gctx_get()->pth_WQ);

    /* clear the suspend queue */
    while ((t = pth_pqueue_delmax(&pth_gctx_get()->pth_SQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_gctx_get()->pth_SQ);

    /* clear the dead queue */
    while ((t = pth_pqueue_delmax(&pth_gctx_get()->pth_DQ)) != NULL)
        pth_tcb_free(t);
    pth_pqueue_init(&pth_gctx_get()->pth_DQ);
    return;
}

/* kill the scheduler ingredients */
intern void pth_scheduler_kill(void)
{
    /* drop all threads */
    pth_scheduler_drop();

    /* remove the internal signal pipe */
    close(pth_gctx_get()->pth_sigpipe[0]);
    close(pth_gctx_get()->pth_sigpipe[1]);
    return;
}


static int pth_sched_check_pth_events(pth_t t) {
    if(!t || !t->events) {
        return 0;
    }

    int n_events_occurred = 0;
    pth_event_t ev = t->events;
    do {
        if (ev->ev_status == PTH_STATUS_PENDING) {
            /* decide if it was pending and now occurred.
             * epoll already told us about FD, TIME, and FUNC type pth events;
             * here we wait check on other pth event types that epoll does not watch */
            int did_occur = FALSE;

            /* Message Port Arrivals */
            if (ev->ev_type == PTH_EVENT_MSG) {
                if (pth_ring_elements(&(ev->ev_args.MSG.mp->mp_queue)) > 0)
                    did_occur = TRUE;
            }
            /* Mutex Release */
            else if (ev->ev_type == PTH_EVENT_MUTEX) {
                if (!(ev->ev_args.MUTEX.mutex->mx_state & PTH_MUTEX_LOCKED))
                    did_occur = TRUE;
            }
            /* Condition Variable Signal */
            else if (ev->ev_type == PTH_EVENT_COND) {
                if (ev->ev_args.COND.cond->cn_state & PTH_COND_SIGNALED) {
                    if (ev->ev_args.COND.cond->cn_state & PTH_COND_BROADCAST)
                        did_occur = TRUE;
                    else {
                        if (!(ev->ev_args.COND.cond->cn_state & PTH_COND_HANDLED)) {
                            ev->ev_args.COND.cond->cn_state |= PTH_COND_HANDLED;
                            did_occur = TRUE;
                        }
                    }
                }
            }
            /* Thread Termination */
            else if (ev->ev_type == PTH_EVENT_TID) {
                if (   (   ev->ev_args.TID.tid == NULL
                        && pth_pqueue_elements(&pth_gctx_get()->pth_DQ) > 0)
                    || (   ev->ev_args.TID.tid != NULL
                        && ev->ev_args.TID.tid->state == ev->ev_goal))
                    did_occur = TRUE;
            }
            /* Signal Set */
            else if (ev->ev_type == PTH_EVENT_SIGS) {
                for (int sig = 1; sig < PTH_NSIG; sig++) {
                    if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
                        /* thread signal handling */
                        if (sigismember(&t->sigpending, sig)) {
                            *(ev->ev_args.SIGS.sig) = sig;
                            sigdelset(&t->sigpending, sig);
                            t->sigpendcnt--;
                            did_occur = TRUE;
                        }
                        /* process signal handling */
                        if (sigismember(&pth_gctx_get()->pth_sigpending, sig)) {
                            if (ev->ev_args.SIGS.sig != NULL)
                                *(ev->ev_args.SIGS.sig) = sig;
                            pth_util_sigdelete(sig);
                            sigdelset(&pth_gctx_get()->pth_sigpending, sig);
                            did_occur = TRUE;
                        }
                        else {
                            sigdelset(&pth_gctx_get()->pth_sigblock, sig);
                            sigaddset(&pth_gctx_get()->pth_sigcatch, sig);
                        }
                    }
                }
            }

            if(did_occur) {
                pth_debug2("pth_sched_eventmanager: event occurred for thread \"%s\"", t->name);
                ev->ev_status = PTH_STATUS_OCCURRED;
            }
        }

        if(ev->ev_status != PTH_STATUS_PENDING) {
            /* it was pending and now it is ready */
            pth_debug2("pth_sched_eventmanager: event occurred for thread \"%s\"", t->name);
            n_events_occurred++;

            /* post-processing for occurred events */

            /* Condition Variable Signal */
            if (ev->ev_type == PTH_EVENT_COND) {
                /* clean signal */
                if (ev->ev_args.COND.cond->cn_state & PTH_COND_SIGNALED) {
                    ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_SIGNALED);
                    ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_BROADCAST);
                    ev->ev_args.COND.cond->cn_state &= ~(PTH_COND_HANDLED);
                }
            }
            /* Custom Event Function */
            else if (ev->ev_type == PTH_EVENT_FUNC) {
                /* call the callback func */
                ev->ev_args.FUNC.func(ev->ev_args.FUNC.arg);
            }
        }
    } while ((ev = ev->ev_next) != t->events);

    return n_events_occurred;
}

static void pth_sched_eventmanager_async(pth_time_t *now) {
    pth_debug1("pth_sched_eventmanager: enter in async mode");

    /* each thread has an epoll */
    int n_threads_waiting = pth_pqueue_elements(&pth_gctx_get()->pth_WQ);
    if(n_threads_waiting < 1) {
        pth_debug1("pth_sched_eventmanager: leave in async mode, no threads waiting");
        return;
    }

    /* check for events without blocking!! */
    struct epoll_event* events_ready = calloc(100, sizeof(struct epoll_event));
    int n_events_ready = pth_sc(epoll_wait)(pth_gctx_get()->main_efd, events_ready, 100, 0);

    /* mark events based on the status we got from epoll */
    for(int i = 0; i < n_events_ready; i++) {
        pth_event_t ev = (pth_event_t) events_ready[i].data.ptr;
        if(!ev) {
            continue;
        }

        /* Filedescriptor I/O */
        if (ev->ev_type == PTH_EVENT_FD) {
            if (((ev->ev_goal & PTH_UNTIL_FD_READABLE) && (events_ready[i].events & EPOLLIN)) ||
                ((ev->ev_goal & PTH_UNTIL_FD_WRITEABLE) && (events_ready[i].events & EPOLLOUT)) ||
                ((ev->ev_goal & PTH_UNTIL_FD_EXCEPTION) && (events_ready[i].events & EPOLLERR))) {
                ev->ev_status = PTH_STATUS_OCCURRED;
            }
        }
        /* Timer */
        else if (ev->ev_type == PTH_EVENT_TIME) {
            uint64_t n_expirations = 0;
            ssize_t rc = pth_sc(read)(ev->ev_args.TIME.fd, &n_expirations, 8);
            if(rc > 0 && n_expirations > 0) {
                ev->ev_status = PTH_STATUS_OCCURRED;
            }
        }
        /* Custom Event Function */
        else if (ev->ev_type == PTH_EVENT_FUNC) {
            uint64_t n_expirations = 0;
            ssize_t rc = pth_sc(read)(ev->ev_args.FUNC.fd, &n_expirations, 8);
            if(rc > 0 && n_expirations > 0) {
                ev->ev_status = PTH_STATUS_OCCURRED;
            }
        }
    }

    /* cleanup */
    free(events_ready);
    events_ready = NULL;

    /* now comes the final cleanup loop where we've to do two jobs:
     * 1 handle all pth event types for all threads
     * 2 move threads with occurred events from the waiting queue to the ready queue */
    pth_t t = pth_pqueue_head(&pth_gctx_get()->pth_WQ);
    pth_t tlast = NULL;
    while (t != NULL) {
        /* do the late handling of the fd I/O and signal
           events in the waiting event ring */
        int n_events_occurred = pth_sched_check_pth_events(t);

        /* cancellation support */
        if (t->cancelreq == TRUE) {
            pth_debug2("pth_sched_eventmanager: cancellation request pending for thread \"%s\"", t->name);
            n_events_occurred++;
        }

        /* walk to next thread in waiting queue */
        tlast = t;
        t = pth_pqueue_walk(&pth_gctx_get()->pth_WQ, t, PTH_WALK_NEXT);

        /*
         * move last thread to ready queue if any events occurred for it.
         * we insert it with a slightly increased queue priority to it a
         * better chance to immediately get scheduled, else the last running
         * thread might immediately get again the CPU which is usually not
         * what we want, because we oven use pth_yield() calls to give others
         * a chance.
         */
        if (n_events_occurred > 0) {
            pth_pqueue_delete(&pth_gctx_get()->pth_WQ, tlast);
            tlast->state = PTH_STATE_READY;
            pth_pqueue_insert(&pth_gctx_get()->pth_RQ, tlast->prio+1, tlast);
            pth_debug2("pth_sched_eventmanager: thread \"%s\" moved from waiting "
                       "to ready queue", tlast->name);
        }
    }

    pth_debug1("pth_sched_eventmanager: leaving");
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
    if (pth_time_cmp((now), &pth_gctx_get()->pth_loadticknext) >= 0) { \
        pth_time_t ttmp; \
        int numready; \
        numready = pth_pqueue_elements(&pth_gctx_get()->pth_RQ); \
        pth_time_set(&ttmp, (now)); \
        do { \
            pth_gctx_get()->pth_loadval = (numready*0.25) + (pth_gctx_get()->pth_loadval*0.75); \
            pth_time_sub(&ttmp, &pth_gctx_get()->pth_loadtickgap); \
        } while (pth_time_cmp(&ttmp, &pth_gctx_get()->pth_loadticknext) >= 0); \
        pth_time_set(&pth_gctx_get()->pth_loadticknext, (now)); \
        pth_time_add(&pth_gctx_get()->pth_loadticknext, &pth_gctx_get()->pth_loadtickgap); \
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
    pth_gctx_get()->pth_sched->state = PTH_STATE_SCHEDULER;

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
        while ((t = pth_pqueue_tail(&pth_gctx_get()->pth_NQ)) != NULL) {
            pth_pqueue_delete(&pth_gctx_get()->pth_NQ, t);
            t->state = PTH_STATE_READY;
            if (pth_gctx_get()->pth_favournew)
                pth_pqueue_insert(&pth_gctx_get()->pth_RQ, pth_pqueue_favorite_prio(&pth_gctx_get()->pth_RQ), t);
            else
                pth_pqueue_insert(&pth_gctx_get()->pth_RQ, PTH_PRIO_STD, t);
            pth_debug2("pth_scheduler: new thread \"%s\" moved to top of ready queue", t->name);
        }

        /*
         * Update average scheduler load
         */
        pth_scheduler_load(&snapshot);

        /*
         * Find next thread in ready queue
         */
        pth_gctx_get()->pth_current = pth_pqueue_delmax(&pth_gctx_get()->pth_RQ);
        if (pth_gctx_get()->pth_current == NULL) {
            fprintf(stderr, "**Pth** SCHEDULER INTERNAL ERROR: "
                            "no more thread(s) available to schedule!?!?\n");
            abort();
        }
        pth_debug4("pth_scheduler: thread \"%s\" selected (prio=%d, qprio=%d)",
                pth_gctx_get()->pth_current->name, pth_gctx_get()->pth_current->prio, pth_gctx_get()->pth_current->q_prio);

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
        if (pth_gctx_get()->pth_current->sigpendcnt > 0) {
            sigpending(&pth_gctx_get()->pth_sigpending);
            for (sig = 1; sig < PTH_NSIG; sig++)
                if (sigismember(&pth_gctx_get()->pth_current->sigpending, sig))
                    if (!sigismember(&pth_gctx_get()->pth_sigpending, sig))
                        kill(getpid(), sig);
        }

        /*
         * Set running start time for new thread
         * and perform a context switch to it
         */
        pth_debug3("pth_scheduler: switching to thread 0x%lx (\"%s\")",
                   (unsigned long)pth_gctx_get()->pth_current, pth_gctx_get()->pth_current->name);

        /* update thread times */
        pth_time_set(&pth_gctx_get()->pth_current->lastran, PTH_TIME_NOW);

        /* update scheduler times */
        pth_time_set(&running, &pth_gctx_get()->pth_current->lastran);
        pth_time_sub(&running, &snapshot);
        pth_time_add(&pth_gctx_get()->pth_sched->running, &running);

        /* ** ENTERING THREAD ** - by switching the machine context */
        pth_gctx_get()->pth_current->dispatches++;
        pth_mctx_switch(&pth_gctx_get()->pth_sched->mctx, &pth_gctx_get()->pth_current->mctx);

        /* update scheduler times */
        pth_time_set(&snapshot, PTH_TIME_NOW);
        pth_debug3("pth_scheduler: cameback from thread 0x%lx (\"%s\")",
                   (unsigned long)pth_gctx_get()->pth_current, pth_gctx_get()->pth_current->name);

        /*
         * Calculate and update the time the previous thread was running
         */
        pth_time_set(&running, &snapshot);
        pth_time_sub(&running, &pth_gctx_get()->pth_current->lastran);
        pth_time_add(&pth_gctx_get()->pth_current->running, &running);
        pth_debug3("pth_scheduler: thread \"%s\" ran %.6f",
                pth_gctx_get()->pth_current->name, pth_time_t2d(&running));

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
        if (pth_gctx_get()->pth_current->sigpendcnt > 0) {
            sigset_t sigstillpending;
            sigpending(&sigstillpending);
            for (sig = 1; sig < PTH_NSIG; sig++) {
                if (sigismember(&pth_gctx_get()->pth_current->sigpending, sig)) {
                    if (!sigismember(&sigstillpending, sig)) {
                        /* thread (and perhaps also process) signal delivered */
                        sigdelset(&pth_gctx_get()->pth_current->sigpending, sig);
                        pth_gctx_get()->pth_current->sigpendcnt--;
                    }
                    else if (!sigismember(&pth_gctx_get()->pth_sigpending, sig)) {
                        /* thread signal not delivered */
                        pth_util_sigdelete(sig);
                    }
                }
            }
        }

        /*
         * Check for stack overflow
         */
        long* sguard = pth_gctx_get()->pth_current->stackguard;
        unsigned int ssize = pth_gctx_get()->pth_current->stacksize;
        int did_overflow = ((ssize > 0 && sguard == NULL) || (sguard != NULL && *sguard != 0xDEAD)) ? 1 : 0;
        if (did_overflow) {
            pth_debug3("pth_scheduler: stack overflow detected for thread 0x%lx (\"%s\")",
                       (unsigned long)pth_gctx_get()->pth_current, pth_gctx_get()->pth_current->name);
            /*
             * if the application doesn't catch SIGSEGVs, we terminate
             * manually with a SIGSEGV now, but output a reasonable message.
             */
            if (sigaction(SIGSEGV, NULL, &sa) == 0) {
                if (sa.sa_handler == SIG_DFL) {
                    fprintf(stderr, "**Pth** STACK OVERFLOW: thread pid_t=0x%lx, name=\"%s\"\n",
                            (unsigned long)pth_gctx_get()->pth_current, pth_gctx_get()->pth_current->name);
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
            pth_gctx_get()->pth_current->join_arg = (void *)0xDEAD;
            pth_gctx_get()->pth_current->state = PTH_STATE_DEAD;
            kill(getpid(), SIGSEGV);
        }

        /*
         * If previous thread is now marked as dead, kick it out
         */
        if (pth_gctx_get()->pth_current->state == PTH_STATE_DEAD) {
            pth_debug2("pth_scheduler: marking thread \"%s\" as dead", pth_gctx_get()->pth_current->name);
            if (!pth_gctx_get()->pth_current->joinable)
                pth_tcb_free(pth_gctx_get()->pth_current);
            else
                pth_pqueue_insert(&pth_gctx_get()->pth_DQ, PTH_PRIO_STD, pth_gctx_get()->pth_current);
            pth_gctx_get()->pth_current = NULL;
        }

        /*
         * If thread wants to wait for an event
         * move it to waiting queue now
         */
        if (pth_gctx_get()->pth_current != NULL && pth_gctx_get()->pth_current->state == PTH_STATE_WAITING) {
            pth_debug2("pth_scheduler: moving thread \"%s\" to waiting queue",
                    pth_gctx_get()->pth_current->name);
            pth_pqueue_insert(&pth_gctx_get()->pth_WQ, pth_gctx_get()->pth_current->prio, pth_gctx_get()->pth_current);
            pth_gctx_get()->pth_current = NULL;
        }

        /*
         * migrate old threads in ready queue into higher
         * priorities to avoid starvation and insert last running
         * thread back into this queue, too.
         */
        pth_pqueue_increase(&pth_gctx_get()->pth_RQ);
        if (pth_gctx_get()->pth_current != NULL)
            pth_pqueue_insert(&pth_gctx_get()->pth_RQ, pth_gctx_get()->pth_current->prio, pth_gctx_get()->pth_current);

        /*
         * Manage the events in the waiting queue, i.e. decide whether their
         * events occurred and move them to the ready queue. But wait only if
         * we have already no new or ready threads.
         */
        if (pth_pqueue_elements(&pth_gctx_get()->pth_RQ) == 0
            && pth_pqueue_elements(&pth_gctx_get()->pth_NQ) == 0) {
            /* still no NEW or READY threads, so we have to wait for new work */
        	if(pth_gctx_get()->pth_is_async) {
        		fprintf(stderr, "**Pth** SCHEDULER INTERNAL ERROR: "
							"we are in async mode and cannot block, but no thread(s) new or ready; "
							"please spawn a thread at minimum priority that can block as needed!\n");
        		abort();
        	} else {
				pth_sched_eventmanager(&snapshot, FALSE /* wait */);
        	}
        }
        else {
			/* already NEW or READY threads exists, so just poll for even more work */
        	if(pth_gctx_get()->pth_is_async) {
        		pth_sched_eventmanager_async(&snapshot);
        	} else {
				pth_sched_eventmanager(&snapshot, TRUE  /* poll */);
        	}
        }
    }

    /* NOTREACHED */
    return NULL;
}

static int rpth_epoll_ctl_helper(int epollfd, int op, int fd, void* data, uint32_t evset) {
    struct epoll_event* epollev = calloc(1, sizeof(struct epoll_event));
    epollev->events = evset;
    epollev->data.ptr = data;
    int ret = epoll_ctl(epollfd, op, fd, epollev);
    free(epollev);
    if(ret == 0) {
        /* all good, 1 fd got added */
        return 1;
    } else if(ret < 0 && errno == EEXIST) {
        /* this didnt get added because it was already there */
        return 0;
    } else {
        /* this didnt get added because of some other error */
        return -1;
    }
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
    struct timeval delay;
    sigset_t oss;
    struct sigaction sa;
    struct sigaction osa[1+PTH_NSIG];
    char minibuf[128];
    int loop_repeat;
    int n_events_ready;
    int sig;
    int epollfd;
    int nepollevs;

    pth_debug2("pth_sched_eventmanager: enter in %s mode",
               dopoll ? "polling" : "waiting");

    /* entry point for internal looping in event handling */
    loop_entry:
    loop_repeat = FALSE;

    /* initialize epoll */
    nepollevs = 0;
    epollfd = epoll_create(1);
    if(epollfd < 0) {
        pth_debug2("pth_sched_eventmanager: epoll_create failed: error %d", errno);
        abort(); // FIXME how to handle error here?
    }

    /* initialize signal status */
    sigpending(&pth_gctx_get()->pth_sigpending);
    sigfillset(&pth_gctx_get()->pth_sigblock);
    sigemptyset(&pth_gctx_get()->pth_sigcatch);
    sigemptyset(&pth_gctx_get()->pth_sigraised);

    /* initialize next timer */
    pth_time_set(&nexttimer_value, PTH_TIME_ZERO);
    nexttimer_thread = NULL;
    nexttimer_ev = NULL;

    /* for all threads in the waiting queue... */
    any_occurred = FALSE;
    for (t = pth_pqueue_head(&pth_gctx_get()->pth_WQ); t != NULL;
         t = pth_pqueue_walk(&pth_gctx_get()->pth_WQ, t, PTH_WALK_NEXT)) {

        /* determine signals we block */
        for (sig = 1; sig < PTH_NSIG; sig++)
            if (!sigismember(&(t->mctx.sigs), sig))
                sigdelset(&pth_gctx_get()->pth_sigblock, sig);

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
                       Here we only track them in the epoll instance. */
                    uint32_t evset = 0;
                    if (ev->ev_goal & PTH_UNTIL_FD_READABLE)
                        evset |= EPOLLIN;
                    if (ev->ev_goal & PTH_UNTIL_FD_WRITEABLE)
                        evset |= EPOLLOUT;
                    if (ev->ev_goal & PTH_UNTIL_FD_EXCEPTION)
                        evset |= EPOLLERR;
                    if(evset != 0) {
                        int retval = rpth_epoll_ctl_helper(epollfd, (int)EPOLL_CTL_ADD, ev->ev_args.FD.fd, ev, evset);
                        if(retval < 0) {
                            ev->ev_status = PTH_STATUS_FAILED;
                            pth_debug3("pth_sched_eventmanager: "
                                       "[I/O] event failed for thread \"%s\" fd %d", t->name, ev->ev_args.FD.fd);
                        } else {
                            nepollevs++;
                        }
                    }
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
                            if (sigismember(&pth_gctx_get()->pth_sigpending, sig)) {
                                if (ev->ev_args.SIGS.sig != NULL)
                                    *(ev->ev_args.SIGS.sig) = sig;
                                pth_util_sigdelete(sig);
                                sigdelset(&pth_gctx_get()->pth_sigpending, sig);
                                this_occurred = TRUE;
                            }
                            else {
                                sigdelset(&pth_gctx_get()->pth_sigblock, sig);
                                sigaddset(&pth_gctx_get()->pth_sigcatch, sig);
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
                            && pth_pqueue_elements(&pth_gctx_get()->pth_DQ) > 0)
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

    /* clear pipe and let select() wait for the read-part of the pipe */
    while (pth_sc(read)(pth_gctx_get()->pth_sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;
    nepollevs += rpth_epoll_ctl_helper(epollfd, (int)EPOLL_CTL_ADD, pth_gctx_get()->pth_sigpipe[0], NULL, (uint32_t)EPOLLIN);

    struct epoll_event* readyevs = calloc(nepollevs, sizeof(struct epoll_event));
    int epoll_timeout;

    if (dopoll) {
        /* do a polling with immediate timeout,
           i.e. check the fd sets only without blocking */
        epoll_timeout = 0;
    }
    else if (nexttimer_ev != NULL) {
        /* do a polling with a timeout set to the next timer,
           i.e. wait for the fd sets or the next timer */
        pth_time_set(&delay, &nexttimer_value);
        pth_time_sub(&delay, now);
        epoll_timeout = (int)((delay.tv_sec*1000) + (delay.tv_usec/1000));
    }
    else {
        /* do a polling without a timeout,
           i.e. wait for the fd sets only with blocking */
        epoll_timeout = -1;
    }

    /* replace signal actions for signals we've to catch for events */
    for (sig = 1; sig < PTH_NSIG; sig++) {
        if (sigismember(&pth_gctx_get()->pth_sigcatch, sig)) {
            sa.sa_handler = pth_sched_eventmanager_sighandler;
            sigfillset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(sig, &sa, &osa[sig]);
        }
    }

    /* allow some signals to be delivered: Either to our
       catching handler or directly to the configured
       handler for signals not catched by events */
    pth_sc(sigprocmask)(SIG_SETMASK, &pth_gctx_get()->pth_sigblock, &oss);

    /* now decide how and do the polling for fd I/O and timers
       WHEN THE SCHEDULER SLEEPS AT ALL, THEN HERE!! */
    n_events_ready = -1;
    if (!(dopoll && nepollevs == 0))
        while ((n_events_ready = pth_sc(epoll_wait)(epollfd, readyevs, nepollevs, epoll_timeout)) < 0
               && errno == EINTR) ;

    /* restore signal mask and actions and handle signals */
    pth_sc(sigprocmask)(SIG_SETMASK, &oss, NULL);
    for (sig = 1; sig < PTH_NSIG; sig++)
        if (sigismember(&pth_gctx_get()->pth_sigcatch, sig))
            sigaction(sig, &osa[sig], NULL);

    /* if the timer elapsed, handle it */
    if (!dopoll && n_events_ready == 0 && nexttimer_ev != NULL) {
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

    /* if an error occurred, avoid confusion in the cleanup loop */
    if (n_events_ready <= 0) {
        memset(readyevs, 0, nepollevs * sizeof(struct epoll_event));
    }

    /* now comes the final cleanup loop where we've to
       do two jobs: first we've to do the late handling of the fd I/O events and
       additionally if a thread has one occurred event, we move it from the
       waiting queue to the ready queue */

    /* set occurred events */
    int i = 0;
    ev = NULL;
    for(i = 0; i < n_events_ready; i++) {
        /* get the pth event that we stored here earlier */
		struct epoll_event* readyev = &readyevs[i];
        ev = (pth_event_t)readyev->data.ptr;

        if(!ev) {
//            /* if the internal signal pipe was used, adjust the select() results */
//            if (!dopoll && rc > 0 && FD_ISSET(pth_gctx_get()->pth_sigpipe[0], &rfds)) {
//                FD_CLR(pth_gctx_get()->pth_sigpipe[0], &rfds);
//                rc--;
//            }
            continue;
        }

        /*
         * Late handling for still not occurred events
         */
        if (ev->ev_status == PTH_STATUS_PENDING) {
            /* Filedescriptor I/O */
            if (ev->ev_type == PTH_EVENT_FD) {
                if (((ev->ev_goal & PTH_UNTIL_FD_READABLE) && (readyev->events & EPOLLIN)) ||
                    ((ev->ev_goal & PTH_UNTIL_FD_WRITEABLE) && (readyev->events & EPOLLOUT)) ||
                    ((ev->ev_goal & PTH_UNTIL_FD_EXCEPTION) && (readyev->events & EPOLLERR))) {
                    ev->ev_status = PTH_STATUS_OCCURRED;
                }
            }
            /* Signal Set */
            else if (ev->ev_type == PTH_EVENT_SIGS) {
                for (sig = 1; sig < PTH_NSIG; sig++) {
                    if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
                        if (sigismember(&pth_gctx_get()->pth_sigraised, sig)) {
                            if (ev->ev_args.SIGS.sig != NULL)
                                *(ev->ev_args.SIGS.sig) = sig;
                            sigdelset(&pth_gctx_get()->pth_sigraised, sig);
                            ev->ev_status = PTH_STATUS_OCCURRED;
                        }
                    }
                }
            }
        }
        /*
         * post-processing for already occurred events
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
    }

    /* for all threads in the waiting queue... */
    t = pth_pqueue_head(&pth_gctx_get()->pth_WQ);
    while (t != NULL) {
        /* do the late handling of the fd I/O and signal
           events in the waiting event ring */
        any_occurred = FALSE;
        if (t->events != NULL) {
            ev = evh = t->events;
            do {
                /* local to global mapping */
                if (ev->ev_status != PTH_STATUS_PENDING) {
                    pth_debug2("pth_sched_eventmanager: event occurred for thread \"%s\"", t->name);
                    any_occurred = TRUE;
                }
            } while ((ev = ev->ev_next) != evh);
        }

        /* cancellation support */
        if (t->cancelreq == TRUE) {
            pth_debug2("pth_sched_eventmanager: cancellation request pending for thread \"%s\"", t->name);
            any_occurred = TRUE;
        }

        /* walk to next thread in waiting queue */
        tlast = t;
        t = pth_pqueue_walk(&pth_gctx_get()->pth_WQ, t, PTH_WALK_NEXT);

        /*
         * move last thread to ready queue if any events occurred for it.
         * we insert it with a slightly increased queue priority to it a
         * better chance to immediately get scheduled, else the last running
         * thread might immediately get again the CPU which is usually not
         * what we want, because we oven use pth_yield() calls to give others
         * a chance.
         */
        if (any_occurred) {
            pth_pqueue_delete(&pth_gctx_get()->pth_WQ, tlast);
            tlast->state = PTH_STATE_READY;
            pth_pqueue_insert(&pth_gctx_get()->pth_RQ, tlast->prio+1, tlast);
            pth_debug2("pth_sched_eventmanager: thread \"%s\" moved from waiting "
                       "to ready queue", tlast->name);
        }
    }

    if(epollfd > -1)
        close(epollfd);
    if(readyevs)
        free(readyevs);

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
    sigaddset(&pth_gctx_get()->pth_sigraised, sig);

    /* write signal to signal pipe in order to awake the select() */
    c = (int)sig;
    pth_sc(write)(pth_gctx_get()->pth_sigpipe[1], &c, sizeof(char));
    return;
}

