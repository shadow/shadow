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
**  pth_event.c: Pth event handling
*/
                             /* ``Those of you who think they
                                  know everything are very annoying
                                  to those of us who do.''
                                                  -- Unknown       */
#include "pth_p.h"

#if cpp

/* pre-declare type of function event callback
   (mainly to workaround va_arg(3) problems below) */
typedef int (*pth_event_func_t)(void *);

/* event structure */
struct pth_event_st {
    struct pth_event_st *ev_next;
    struct pth_event_st *ev_prev;
    pth_status_t ev_status;
    int ev_type;
    int ev_goal;
    union {
        struct { int fd; }                                          FD;
        struct { int *n; int nfd; fd_set *rfds, *wfds, *efds; }     SELECT;
        struct { sigset_t *sigs; int *sig; }                        SIGS;
        struct { pth_time_t tv; }                                   TIME;
        struct { pth_msgport_t mp; }                                MSG;
        struct { pth_mutex_t *mutex; }                              MUTEX;
        struct { pth_cond_t *cond; }                                COND;
        struct { pth_t tid; }                                       TID;
        struct { pth_event_func_t func; void *arg; pth_time_t tv; } FUNC;
    } ev_args;
};

#endif /* cpp */

/* event structure destructor */
static void pth_event_destructor(void *vp)
{
    /* free this single(!) event. That it is just a single event is a
       requirement for pth_event(PTH_MODE_STATIC, ...), or else we would
       get into horrible trouble on asychronous cleanups */
    pth_event_free((pth_event_t)vp, PTH_FREE_THIS);
    return;
}

/* event structure constructor */
pth_event_t pth_event(unsigned long spec, ...)
{
    pth_event_t ev;
    pth_key_t *ev_key;
    va_list ap;

    va_start(ap, spec);

    /* allocate new or reuse static or supplied event structure */
    if (spec & PTH_MODE_REUSE) {
        /* reuse supplied event structure */
        ev = va_arg(ap, pth_event_t);
    }
    else if (spec & PTH_MODE_STATIC) {
        /* reuse static event structure */
        ev_key = va_arg(ap, pth_key_t *);
        if (*ev_key == PTH_KEY_INIT)
            pth_key_create(ev_key, pth_event_destructor);
        ev = (pth_event_t)pth_key_getdata(*ev_key);
        if (ev == NULL) {
            ev = (pth_event_t)malloc(sizeof(struct pth_event_st));
            pth_key_setdata(*ev_key, ev);
        }
    }
    else {
        /* allocate new dynamic event structure */
        ev = (pth_event_t)malloc(sizeof(struct pth_event_st));
    }
    if (ev == NULL)
        return pth_error((pth_event_t)NULL, errno);

    /* create new event ring out of event or insert into existing ring */
    if (spec & PTH_MODE_CHAIN) {
        pth_event_t ch = va_arg(ap, pth_event_t);
        ev->ev_prev = ch->ev_prev;
        ev->ev_next = ch;
        ev->ev_prev->ev_next = ev;
        ev->ev_next->ev_prev = ev;
    }
    else {
        ev->ev_prev = ev;
        ev->ev_next = ev;
    }

    /* initialize common ingredients */
    ev->ev_status = PTH_STATUS_PENDING;

    /* initialize event specific ingredients */
    if (spec & PTH_EVENT_FD) {
        /* filedescriptor event */
        int fd = va_arg(ap, int);
        if (!pth_util_fd_valid(fd))
            return pth_error((pth_event_t)NULL, EBADF);
        ev->ev_type = PTH_EVENT_FD;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_FD_READABLE|\
                                    PTH_UNTIL_FD_WRITEABLE|\
                                    PTH_UNTIL_FD_EXCEPTION));
        ev->ev_args.FD.fd = fd;
    }
    else if (spec & PTH_EVENT_SELECT) {
        /* filedescriptor set select event */
        int *n = va_arg(ap, int *);
        int nfd = va_arg(ap, int);
        fd_set *rfds = va_arg(ap, fd_set *);
        fd_set *wfds = va_arg(ap, fd_set *);
        fd_set *efds = va_arg(ap, fd_set *);
        ev->ev_type = PTH_EVENT_SELECT;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.SELECT.n    = n;
        ev->ev_args.SELECT.nfd  = nfd;
        ev->ev_args.SELECT.rfds = rfds;
        ev->ev_args.SELECT.wfds = wfds;
        ev->ev_args.SELECT.efds = efds;
    }
    else if (spec & PTH_EVENT_SIGS) {
        /* signal set event */
        sigset_t *sigs = va_arg(ap, sigset_t *);
        int *sig = va_arg(ap, int *);
        ev->ev_type = PTH_EVENT_SIGS;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.SIGS.sigs = sigs;
        ev->ev_args.SIGS.sig = sig;
    }
    else if (spec & PTH_EVENT_TIME) {
        /* interrupt request event */
        pth_time_t tv = va_arg(ap, pth_time_t);
        ev->ev_type = PTH_EVENT_TIME;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.TIME.tv = tv;
    }
    else if (spec & PTH_EVENT_MSG) {
        /* message port event */
        pth_msgport_t mp = va_arg(ap, pth_msgport_t);
        ev->ev_type = PTH_EVENT_MSG;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.MSG.mp = mp;
    }
    else if (spec & PTH_EVENT_MUTEX) {
        /* mutual exclusion lock */
        pth_mutex_t *mutex = va_arg(ap, pth_mutex_t *);
        ev->ev_type = PTH_EVENT_MUTEX;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.MUTEX.mutex = mutex;
    }
    else if (spec & PTH_EVENT_COND) {
        /* condition variable */
        pth_cond_t *cond = va_arg(ap, pth_cond_t *);
        ev->ev_type = PTH_EVENT_COND;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.COND.cond = cond;
    }
    else if (spec & PTH_EVENT_TID) {
        /* thread id event */
        pth_t tid = va_arg(ap, pth_t);
        int goal;
        ev->ev_type = PTH_EVENT_TID;
        if (spec & PTH_UNTIL_TID_NEW)
            goal = PTH_STATE_NEW;
        else if (spec & PTH_UNTIL_TID_READY)
            goal = PTH_STATE_READY;
        else if (spec & PTH_UNTIL_TID_WAITING)
            goal = PTH_STATE_WAITING;
        else if (spec & PTH_UNTIL_TID_DEAD)
            goal = PTH_STATE_DEAD;
        else
            goal = PTH_STATE_READY;
        ev->ev_goal = goal;
        ev->ev_args.TID.tid = tid;
    }
    else if (spec & PTH_EVENT_FUNC) {
        /* custom function event */
        ev->ev_type = PTH_EVENT_FUNC;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.FUNC.func  = va_arg(ap, pth_event_func_t);
        ev->ev_args.FUNC.arg   = va_arg(ap, void *);
        ev->ev_args.FUNC.tv    = va_arg(ap, pth_time_t);
    }
    else
        return pth_error((pth_event_t)NULL, EINVAL);

    va_end(ap);

    /* return event */
    return ev;
}

/* determine type of event */
unsigned long pth_event_typeof(pth_event_t ev)
{
    if (ev == NULL)
        return pth_error(0, EINVAL);
    return (ev->ev_type | ev->ev_goal);
}

/* event extractor */
int pth_event_extract(pth_event_t ev, ...)
{
    va_list ap;

    if (ev == NULL)
        return pth_error(FALSE, EINVAL);
    va_start(ap, ev);

    /* extract event specific ingredients */
    if (ev->ev_type & PTH_EVENT_FD) {
        /* filedescriptor event */
        int *fd = va_arg(ap, int *);
        *fd = ev->ev_args.FD.fd;
    }
    else if (ev->ev_type & PTH_EVENT_SIGS) {
        /* signal set event */
        sigset_t **sigs = va_arg(ap, sigset_t **);
        int **sig = va_arg(ap, int **);
        *sigs = ev->ev_args.SIGS.sigs;
        *sig = ev->ev_args.SIGS.sig;
    }
    else if (ev->ev_type & PTH_EVENT_TIME) {
        /* interrupt request event */
        pth_time_t *tv = va_arg(ap, pth_time_t *);
        *tv = ev->ev_args.TIME.tv;
    }
    else if (ev->ev_type & PTH_EVENT_MSG) {
        /* message port event */
        pth_msgport_t *mp = va_arg(ap, pth_msgport_t *);
        *mp = ev->ev_args.MSG.mp;
    }
    else if (ev->ev_type & PTH_EVENT_MUTEX) {
        /* mutual exclusion lock */
        pth_mutex_t **mutex = va_arg(ap, pth_mutex_t **);
        *mutex = ev->ev_args.MUTEX.mutex;
    }
    else if (ev->ev_type & PTH_EVENT_COND) {
        /* condition variable */
        pth_cond_t **cond = va_arg(ap, pth_cond_t **);
        *cond = ev->ev_args.COND.cond;
    }
    else if (ev->ev_type & PTH_EVENT_TID) {
        /* thread id event */
        pth_t *tid = va_arg(ap, pth_t *);
        *tid = ev->ev_args.TID.tid;
    }
    else if (ev->ev_type & PTH_EVENT_FUNC) {
        /* custom function event */
        pth_event_func_t *func = va_arg(ap, pth_event_func_t *);
        void **arg             = va_arg(ap, void **);
        pth_time_t *tv         = va_arg(ap, pth_time_t *);
        *func = ev->ev_args.FUNC.func;
        *arg  = ev->ev_args.FUNC.arg;
        *tv   = ev->ev_args.FUNC.tv;
    }
    else
        return pth_error(FALSE, EINVAL);
    va_end(ap);
    return TRUE;
}

/* concatenate one or more events or event rings */
pth_event_t pth_event_concat(pth_event_t evf, ...)
{
    pth_event_t evc; /* current event */
    pth_event_t evn; /* next event */
    pth_event_t evl; /* last event */
    pth_event_t evt; /* temporary event */
    va_list ap;

    if (evf == NULL)
        return pth_error((pth_event_t)NULL, EINVAL);

    /* open ring */
    va_start(ap, evf);
    evc = evf;
    evl = evc->ev_next;

    /* attach additional rings */
    while ((evn = va_arg(ap, pth_event_t)) != NULL) {
        evc->ev_next = evn;
        evt = evn->ev_prev;
        evn->ev_prev = evc;
        evc = evt;
    }

    /* close ring */
    evc->ev_next = evl;
    evl->ev_prev = evc;
    va_end(ap);

    return evf;
}

/* isolate one event from a possible appended event ring */
pth_event_t pth_event_isolate(pth_event_t ev)
{
    pth_event_t ring;

    if (ev == NULL)
        return pth_error((pth_event_t)NULL, EINVAL);
    ring = NULL;
    if (!(ev->ev_next == ev && ev->ev_prev == ev)) {
        ring = ev->ev_next;
        ev->ev_prev->ev_next = ev->ev_next;
        ev->ev_next->ev_prev = ev->ev_prev;
        ev->ev_prev = ev;
        ev->ev_next = ev;
    }
    return ring;
}

/* determine status of the event */
pth_status_t pth_event_status(pth_event_t ev)
{
    if (ev == NULL)
        return pth_error(FALSE, EINVAL);
    return ev->ev_status;
}

/* walk to next or previous event in an event ring */
pth_event_t pth_event_walk(pth_event_t ev, unsigned int direction)
{
    if (ev == NULL)
        return pth_error((pth_event_t)NULL, EINVAL);
    do {
        if (direction & PTH_WALK_NEXT)
            ev = ev->ev_next;
        else if (direction & PTH_WALK_PREV)
            ev = ev->ev_prev;
        else
            return pth_error((pth_event_t)NULL, EINVAL);
    } while ((direction & PTH_UNTIL_OCCURRED) && (ev->ev_status != PTH_STATUS_OCCURRED));
    return ev;
}

/* deallocate an event structure */
int pth_event_free(pth_event_t ev, int mode)
{
    pth_event_t evc;
    pth_event_t evn;

    if (ev == NULL)
        return pth_error(FALSE, EINVAL);
    if (mode == PTH_FREE_THIS) {
        ev->ev_prev->ev_next = ev->ev_next;
        ev->ev_next->ev_prev = ev->ev_prev;
        free(ev);
    }
    else if (mode == PTH_FREE_ALL) {
        evc = ev;
        do {
            evn = evc->ev_next;
            free(evc);
            evc = evn;
        } while (evc != ev);
    }
    return TRUE;
}

/* wait for one or more events */
int pth_wait(pth_event_t ev_ring)
{
    int nonpending;
    pth_event_t ev;

    /* at least a waiting ring is required */
    if (ev_ring == NULL)
        return pth_error(-1, EINVAL);
    pth_debug2("pth_wait: enter from thread \"%s\"", pth_current->name);

    /* mark all events in waiting ring as still pending */
    ev = ev_ring;
    do {
        ev->ev_status = PTH_STATUS_PENDING;
        pth_debug2("pth_wait: waiting on event 0x%lx", (unsigned long)ev);
        ev = ev->ev_next;
    } while (ev != ev_ring);

    /* link event ring to current thread */
    pth_current->events = ev_ring;

    /* move thread into waiting state
       and transfer control to scheduler */
    pth_current->state = PTH_STATE_WAITING;
    pth_yield(NULL);

    /* check for cancellation */
    pth_cancel_point();

    /* unlink event ring from current thread */
    pth_current->events = NULL;

    /* count number of actually occurred (or failed) events */
    ev = ev_ring;
    nonpending = 0;
    do {
        if (ev->ev_status != PTH_STATUS_PENDING) {
            pth_debug2("pth_wait: non-pending event 0x%lx", (unsigned long)ev);
            nonpending++;
        }
        ev = ev->ev_next;
    } while (ev != ev_ring);

    /* leave to current thread with number of occurred events */
    pth_debug2("pth_wait: leave to thread \"%s\"", pth_current->name);
    return nonpending;
}

