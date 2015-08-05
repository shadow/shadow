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
**  pth_sync.c: Pth synchronization facilities
*/
                             /* ``It is hard to fly with
                                  the eagles when you work
                                  with the turkeys.''
                                          -- Unknown  */
#include "pth_p.h"

/*
**  Mutual Exclusion Locks
*/

int pth_mutex_init(pth_mutex_t *mutex)
{
    if (mutex == NULL)
        return pth_error(FALSE, EINVAL);
    mutex->mx_state = PTH_MUTEX_INITIALIZED;
    mutex->mx_owner = NULL;
    mutex->mx_count = 0;
    return TRUE;
}

int pth_mutex_acquire(pth_mutex_t *mutex, int tryonly, pth_event_t ev_extra)
{
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_event_t ev;

    pth_debug2("pth_mutex_acquire: called from thread \"%s\"", pth_current->name);

    /* consistency checks */
    if (mutex == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(mutex->mx_state & PTH_MUTEX_INITIALIZED))
        return pth_error(FALSE, EDEADLK);

    /* still not locked, so simply acquire mutex? */
    if (!(mutex->mx_state & PTH_MUTEX_LOCKED)) {
        mutex->mx_state |= PTH_MUTEX_LOCKED;
        mutex->mx_owner = pth_current;
        mutex->mx_count = 1;
        pth_ring_append(&(pth_current->mutexring), &(mutex->mx_node));
        pth_debug1("pth_mutex_acquire: immediately locking mutex");
        return TRUE;
    }

    /* already locked by caller? */
    if (mutex->mx_count >= 1 && mutex->mx_owner == pth_current) {
        /* recursive lock */
        mutex->mx_count++;
        pth_debug1("pth_mutex_acquire: recursive locking");
        return TRUE;
    }

    /* should we just tryonly? */
    if (tryonly)
        return pth_error(FALSE, EBUSY);

    /* else wait for mutex to become unlocked.. */
    pth_debug1("pth_mutex_acquire: wait until mutex is unlocked");
    for (;;) {
        ev = pth_event(PTH_EVENT_MUTEX|PTH_MODE_STATIC, &ev_key, mutex);
        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);
        pth_wait(ev);
        if (ev_extra != NULL) {
            pth_event_isolate(ev);
            if (pth_event_status(ev) == PTH_STATUS_PENDING)
                return pth_error(FALSE, EINTR);
        }
        if (!(mutex->mx_state & PTH_MUTEX_LOCKED))
            break;
    }

    /* now it's again unlocked, so acquire mutex */
    pth_debug1("pth_mutex_acquire: locking mutex");
    mutex->mx_state |= PTH_MUTEX_LOCKED;
    mutex->mx_owner = pth_current;
    mutex->mx_count = 1;
    pth_ring_append(&(pth_current->mutexring), &(mutex->mx_node));
    return TRUE;
}

int pth_mutex_release(pth_mutex_t *mutex)
{
    /* consistency checks */
    if (mutex == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(mutex->mx_state & PTH_MUTEX_INITIALIZED))
        return pth_error(FALSE, EDEADLK);
    if (!(mutex->mx_state & PTH_MUTEX_LOCKED))
        return pth_error(FALSE, EDEADLK);
    if (mutex->mx_owner != pth_current)
        return pth_error(FALSE, EACCES);

    /* decrement recursion counter and release mutex */
    mutex->mx_count--;
    if (mutex->mx_count <= 0) {
        mutex->mx_state &= ~(PTH_MUTEX_LOCKED);
        mutex->mx_owner = NULL;
        mutex->mx_count = 0;
        pth_ring_delete(&(pth_current->mutexring), &(mutex->mx_node));
    }
    return TRUE;
}

intern void pth_mutex_releaseall(pth_t thread)
{
    pth_ringnode_t *rn, *rnf;

    if (thread == NULL)
        return;
    /* iterate over all mutexes of thread */
    rn = rnf = pth_ring_first(&(thread->mutexring));
    while (rn != NULL) {
        pth_mutex_release((pth_mutex_t *)rn);
        rn = pth_ring_next(&(thread->mutexring), rn);
        if (rn == rnf)
            break;
    }
    return;
}

/*
**  Read-Write Locks
*/

int pth_rwlock_init(pth_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return pth_error(FALSE, EINVAL);
    rwlock->rw_state = PTH_RWLOCK_INITIALIZED;
    rwlock->rw_readers = 0;
    pth_mutex_init(&(rwlock->rw_mutex_rd));
    pth_mutex_init(&(rwlock->rw_mutex_rw));
    return TRUE;
}

int pth_rwlock_acquire(pth_rwlock_t *rwlock, int op, int tryonly, pth_event_t ev_extra)
{
    /* consistency checks */
    if (rwlock == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(rwlock->rw_state & PTH_RWLOCK_INITIALIZED))
        return pth_error(FALSE, EDEADLK);

    /* acquire lock */
    if (op == PTH_RWLOCK_RW) {
        /* read-write lock is simple */
        if (!pth_mutex_acquire(&(rwlock->rw_mutex_rw), tryonly, ev_extra))
            return FALSE;
        rwlock->rw_mode = PTH_RWLOCK_RW;
    }
    else {
        /* read-only lock is more complicated to get right */
        if (!pth_mutex_acquire(&(rwlock->rw_mutex_rd), tryonly, ev_extra))
            return FALSE;
        rwlock->rw_readers++;
        if (rwlock->rw_readers == 1) {
            if (!pth_mutex_acquire(&(rwlock->rw_mutex_rw), tryonly, ev_extra)) {
                rwlock->rw_readers--;
                pth_shield { pth_mutex_release(&(rwlock->rw_mutex_rd)); }
                return FALSE;
            }
        }
        rwlock->rw_mode = PTH_RWLOCK_RD;
        pth_mutex_release(&(rwlock->rw_mutex_rd));
    }
    return TRUE;
}

int pth_rwlock_release(pth_rwlock_t *rwlock)
{
    /* consistency checks */
    if (rwlock == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(rwlock->rw_state & PTH_RWLOCK_INITIALIZED))
        return pth_error(FALSE, EDEADLK);

    /* release lock */
    if (rwlock->rw_mode == PTH_RWLOCK_RW) {
        /* read-write unlock is simple */
        if (!pth_mutex_release(&(rwlock->rw_mutex_rw)))
            return FALSE;
    }
    else {
        /* read-only unlock is more complicated to get right */
        if (!pth_mutex_acquire(&(rwlock->rw_mutex_rd), FALSE, NULL))
            return FALSE;
        rwlock->rw_readers--;
        if (rwlock->rw_readers == 0) {
            if (!pth_mutex_release(&(rwlock->rw_mutex_rw))) {
                rwlock->rw_readers++;
                pth_shield { pth_mutex_release(&(rwlock->rw_mutex_rd)); }
                return FALSE;
            }
        }
        rwlock->rw_mode = PTH_RWLOCK_RD;
        pth_mutex_release(&(rwlock->rw_mutex_rd));
    }
    return TRUE;
}

/*
**  Condition Variables
*/

int pth_cond_init(pth_cond_t *cond)
{
    if (cond == NULL)
        return pth_error(FALSE, EINVAL);
    cond->cn_state   = PTH_COND_INITIALIZED;
    cond->cn_waiters = 0;
    return TRUE;
}

static void pth_cond_cleanup_handler(void *_cleanvec)
{
    pth_mutex_t *mutex = (pth_mutex_t *)(((void **)_cleanvec)[0]);
    pth_cond_t  *cond  = (pth_cond_t  *)(((void **)_cleanvec)[1]);

    /* re-acquire mutex when pth_cond_await() is cancelled
       in order to restore the condition variable semantics */
    pth_mutex_acquire(mutex, FALSE, NULL);

    /* fix number of waiters */
    cond->cn_waiters--;
    return;
}

int pth_cond_await(pth_cond_t *cond, pth_mutex_t *mutex, pth_event_t ev_extra)
{
    static pth_key_t ev_key = PTH_KEY_INIT;
    void *cleanvec[2];
    pth_event_t ev;

    /* consistency checks */
    if (cond == NULL || mutex == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(cond->cn_state & PTH_COND_INITIALIZED))
        return pth_error(FALSE, EDEADLK);

    /* check whether we can do a short-circuit wait */
    if (    (cond->cn_state & PTH_COND_SIGNALED)
        && !(cond->cn_state & PTH_COND_BROADCAST)) {
        cond->cn_state &= ~(PTH_COND_SIGNALED);
        cond->cn_state &= ~(PTH_COND_BROADCAST);
        cond->cn_state &= ~(PTH_COND_HANDLED);
        return TRUE;
    }

    /* add us to the number of waiters */
    cond->cn_waiters++;

    /* release mutex (caller had to acquire it first) */
    pth_mutex_release(mutex);

    /* wait until the condition is signaled */
    ev = pth_event(PTH_EVENT_COND|PTH_MODE_STATIC, &ev_key, cond);
    if (ev_extra != NULL)
        pth_event_concat(ev, ev_extra, NULL);
    cleanvec[0] = mutex;
    cleanvec[1] = cond;
    pth_cleanup_push(pth_cond_cleanup_handler, cleanvec);
    pth_wait(ev);
    pth_cleanup_pop(FALSE);
    if (ev_extra != NULL)
        pth_event_isolate(ev);

    /* reacquire mutex */
    pth_mutex_acquire(mutex, FALSE, NULL);

    /* remove us from the number of waiters */
    cond->cn_waiters--;

    /* release mutex (caller had to acquire it first) */
    return TRUE;
}

int pth_cond_notify(pth_cond_t *cond, int broadcast)
{
    /* consistency checks */
    if (cond == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(cond->cn_state & PTH_COND_INITIALIZED))
        return pth_error(FALSE, EDEADLK);

    /* do something only if there is at least one waiters (POSIX semantics) */
    if (cond->cn_waiters > 0) {
        /* signal the condition */
        cond->cn_state |= PTH_COND_SIGNALED;
        if (broadcast)
            cond->cn_state |= PTH_COND_BROADCAST;
        else
            cond->cn_state &= ~(PTH_COND_BROADCAST);
        cond->cn_state &= ~(PTH_COND_HANDLED);

        /* and give other threads a chance to awake */
        pth_yield(NULL);
    }

    /* return to caller */
    return TRUE;
}

/*
**  Barriers
*/

int pth_barrier_init(pth_barrier_t *barrier, int threshold)
{
    if (barrier == NULL || threshold <= 0)
        return pth_error(FALSE, EINVAL);
    if (!pth_mutex_init(&(barrier->br_mutex)))
        return FALSE;
    if (!pth_cond_init(&(barrier->br_cond)))
        return FALSE;
    barrier->br_state     = PTH_BARRIER_INITIALIZED;
    barrier->br_threshold = threshold;
    barrier->br_count     = threshold;
    barrier->br_cycle     = FALSE;
    return TRUE;
}

int pth_barrier_reach(pth_barrier_t *barrier)
{
    int cancel, cycle;
    int rv;

    if (barrier == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(barrier->br_state & PTH_BARRIER_INITIALIZED))
        return pth_error(FALSE, EINVAL);

    if (!pth_mutex_acquire(&(barrier->br_mutex), FALSE, NULL))
        return FALSE;
    cycle = barrier->br_cycle;
    if (--(barrier->br_count) == 0) {
        /* last thread reached the barrier */
        barrier->br_cycle   = !(barrier->br_cycle);
        barrier->br_count   = barrier->br_threshold;
        if ((rv = pth_cond_notify(&(barrier->br_cond), TRUE)))
            rv = PTH_BARRIER_TAILLIGHT;
    }
    else {
        /* wait until remaining threads have reached the barrier, too */
        pth_cancel_state(PTH_CANCEL_DISABLE, &cancel);
        if (barrier->br_threshold == barrier->br_count)
            rv = PTH_BARRIER_HEADLIGHT;
        else
            rv = TRUE;
        while (cycle == barrier->br_cycle) {
            if (!(rv = pth_cond_await(&(barrier->br_cond), &(barrier->br_mutex), NULL)))
                break;
        }
        pth_cancel_state(cancel, NULL);
    }
    pth_mutex_release(&(barrier->br_mutex));
    return rv;
}

