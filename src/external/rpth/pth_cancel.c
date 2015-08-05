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
**  pth_cancel.c: Pth thread cancellation
*/
                             /* ``Study it forever and you'll still wonder.
                                  Fly it once and you'll know.''
                                                       -- Henry Spencer */
#include "pth_p.h"

/* set cancellation state */
void pth_cancel_state(int newstate, int *oldstate)
{
    if (oldstate != NULL)
        *oldstate = pth_current->cancelstate;
    if (newstate != 0)
        pth_current->cancelstate = newstate;
    return;
}

/* enter a cancellation point */
void pth_cancel_point(void)
{
    if (   pth_current->cancelreq == TRUE
        && pth_current->cancelstate & PTH_CANCEL_ENABLE) {
        /* avoid looping if cleanup handlers contain cancellation points */
        pth_current->cancelreq = FALSE;
        pth_debug2("pth_cancel_point: terminating cancelled thread \"%s\"", pth_current->name);
        pth_exit(PTH_CANCELED);
    }
    return;
}

/* cancel a thread (the friendly way) */
int pth_cancel(pth_t thread)
{
    pth_pqueue_t *q;

    if (thread == NULL)
        return pth_error(FALSE, EINVAL);

    /* the current thread cannot be cancelled */
    if (thread == pth_current)
        return pth_error(FALSE, EINVAL);

    /* the thread has to be at least still alive */
    if (thread->state == PTH_STATE_DEAD)
        return pth_error(FALSE, EPERM);

    /* now mark the thread as cancelled */
    thread->cancelreq = TRUE;

    /* when cancellation is enabled in async mode we cancel the thread immediately */
    if (   thread->cancelstate & PTH_CANCEL_ENABLE
        && thread->cancelstate & PTH_CANCEL_ASYNCHRONOUS) {

        /* remove thread from its queue */
        switch (thread->state) {
            case PTH_STATE_NEW:     q = &pth_NQ; break;
            case PTH_STATE_READY:   q = &pth_RQ; break;
            case PTH_STATE_WAITING: q = &pth_WQ; break;
            default:                q = NULL;
        }
        if (q == NULL)
            return pth_error(FALSE, ESRCH);
        if (!pth_pqueue_contains(q, thread))
            return pth_error(FALSE, ESRCH);
        pth_pqueue_delete(q, thread);

        /* execute cleanups */
        pth_thread_cleanup(thread);

        /* and now either kick it out or move it to dead queue */
        if (!thread->joinable) {
            pth_debug2("pth_cancel: kicking out cancelled thread \"%s\" immediately", thread->name);
            pth_tcb_free(thread);
        }
        else {
            pth_debug2("pth_cancel: moving cancelled thread \"%s\" to dead queue", thread->name);
            thread->join_arg = PTH_CANCELED;
            thread->state = PTH_STATE_DEAD;
            pth_pqueue_insert(&pth_DQ, PTH_PRIO_STD, thread);
        }
    }
    return TRUE;
}

/* abort a thread (the cruel way) */
int pth_abort(pth_t thread)
{
    if (thread == NULL)
        return pth_error(FALSE, EINVAL);

    /* the current thread cannot be aborted */
    if (thread == pth_current)
        return pth_error(FALSE, EINVAL);

    if (thread->state == PTH_STATE_DEAD && thread->joinable) {
        /* if thread is already terminated, just join it */
        if (!pth_join(thread, NULL))
            return FALSE;
    }
    else {
        /* else force it to be detached and cancel it asynchronously */
        thread->joinable = FALSE;
        thread->cancelstate = (PTH_CANCEL_ENABLE|PTH_CANCEL_ASYNCHRONOUS);
        if (!pth_cancel(thread))
            return FALSE;
    }
    return TRUE;
}

