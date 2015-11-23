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
**  pth_attr.c: Pth thread attributes
*/
                             /* ``Unix -- where you can do anything
                                  in two keystrokes, or less...'' 
                                                     -- Unknown  */
#include "pth_p.h"

#if cpp

enum {
    PTH_ATTR_GET,
    PTH_ATTR_SET
};

struct pth_attr_st {
    pth_t        a_tid;
    int          a_prio;
    int          a_dispatches;
    char         a_name[PTH_TCB_NAMELEN];
    int          a_joinable;
    unsigned int a_cancelstate;
    unsigned int a_stacksize;
    char        *a_stackaddr;
};

#endif /* cpp */

pth_attr_t pth_attr_of(pth_t t)
{
    pth_attr_t a;

    if (t == NULL)
        return pth_error((pth_attr_t)NULL, EINVAL);
    if ((a = (pth_attr_t)malloc(sizeof(struct pth_attr_st))) == NULL)
        return pth_error((pth_attr_t)NULL, ENOMEM);
    a->a_tid = t;
    return a;
}

pth_attr_t pth_attr_new(void)
{
    pth_attr_t a;

    if ((a = (pth_attr_t)malloc(sizeof(struct pth_attr_st))) == NULL)
        return pth_error((pth_attr_t)NULL, ENOMEM);
    a->a_tid = NULL;
    pth_attr_init(a);
    return a;
}

int pth_attr_destroy(pth_attr_t a)
{
    if (a == NULL)
        return pth_error(FALSE, EINVAL);
    free(a);
    return TRUE;
}

int pth_attr_init(pth_attr_t a)
{
    if (a == NULL)
        return pth_error(FALSE, EINVAL);
    if (a->a_tid != NULL)
        return pth_error(FALSE, EPERM);
    a->a_prio = PTH_PRIO_STD;
    pth_util_cpystrn(a->a_name, "unknown", PTH_TCB_NAMELEN);
    a->a_dispatches = 0;
    a->a_joinable = TRUE;
    a->a_cancelstate = PTH_CANCEL_DEFAULT;
    a->a_stacksize = 64*1024;
    a->a_stackaddr = NULL;
    return TRUE;
}

int pth_attr_get(pth_attr_t a, int op, ...)
{
    va_list ap;
    int rc;

    va_start(ap, op);
    rc = pth_attr_ctrl(PTH_ATTR_GET, a, op, ap);
    va_end(ap);
    return rc;
}

int pth_attr_set(pth_attr_t a, int op, ...)
{
    va_list ap;
    int rc;

    va_start(ap, op);
    rc = pth_attr_ctrl(PTH_ATTR_SET, a, op, ap);
    va_end(ap);
    return rc;
}

intern int pth_attr_ctrl(int cmd, pth_attr_t a, int op, va_list ap)
{
    if (a == NULL)
        return pth_error(FALSE, EINVAL);
    switch (op) {
        case PTH_ATTR_PRIO: {
            /* priority */
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = (a->a_tid != NULL ? &a->a_tid->prio : &a->a_prio);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->prio : &a->a_prio);
                dst = va_arg(ap, int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_NAME: {
            /* name */
            if (cmd == PTH_ATTR_SET) {
                char *src, *dst;
                src = va_arg(ap, char *);
                dst = (a->a_tid != NULL ? a->a_tid->name : a->a_name);
                pth_util_cpystrn(dst, src, PTH_TCB_NAMELEN);
            }
            else {
                char *src, **dst;
                src = (a->a_tid != NULL ? a->a_tid->name : a->a_name);
                dst = va_arg(ap, char **);
                *dst = src;
            }
            break;
        }
        case PTH_ATTR_DISPATCHES: {
            /* incremented on every context switch */
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = (a->a_tid != NULL ? &a->a_tid->dispatches : &a->a_dispatches);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->dispatches : &a->a_dispatches);
                dst = va_arg(ap, int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_JOINABLE: {
            /* detachment type */
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = (a->a_tid != NULL ? &a->a_tid->joinable : &a->a_joinable);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->joinable : &a->a_joinable);
                dst = va_arg(ap, int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_CANCEL_STATE: {
            /* cancellation state */
            unsigned int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, unsigned int);
                dst = (a->a_tid != NULL ? &a->a_tid->cancelstate : &a->a_cancelstate);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->cancelstate : &a->a_cancelstate);
                dst = va_arg(ap, unsigned int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_STACK_SIZE: {
            /* stack size */
            unsigned int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid != NULL)
                    return pth_error(FALSE, EPERM);
                src = &val; val = va_arg(ap, unsigned int);
                dst = &a->a_stacksize;
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->stacksize : &a->a_stacksize);
                dst = va_arg(ap, unsigned int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_STACK_ADDR: {
            /* stack address */
            char *val, **src, **dst;
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid != NULL)
                    return pth_error(FALSE, EPERM);
                src = &val; val = va_arg(ap, char *);
                dst = &a->a_stackaddr;
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->stack : &a->a_stackaddr);
                dst = va_arg(ap, char **);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_TIME_SPAWN: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            dst = va_arg(ap, pth_time_t *);
            if (a->a_tid != NULL)
                pth_time_set(dst, &a->a_tid->spawned);
            else
                pth_time_set(dst, PTH_TIME_ZERO);
            break;
        }
        case PTH_ATTR_TIME_LAST: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            dst = va_arg(ap, pth_time_t *);
            if (a->a_tid != NULL)
                pth_time_set(dst, &a->a_tid->lastran);
            else
                pth_time_set(dst, PTH_TIME_ZERO);
            break;
        }
        case PTH_ATTR_TIME_RAN: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            dst = va_arg(ap, pth_time_t *);
            if (a->a_tid != NULL)
                pth_time_set(dst, &a->a_tid->running);
            else
                pth_time_set(dst, PTH_TIME_ZERO);
            break;
        }
        case PTH_ATTR_START_FUNC: {
            void *(**dst)(void *);
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            if (a->a_tid == NULL)
                return pth_error(FALSE, EACCES);
            dst = (void *(**)(void *))va_arg(ap, void *);
            *dst = a->a_tid->start_func;
            break;
        }
        case PTH_ATTR_START_ARG: {
            void **dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            if (a->a_tid == NULL)
                return pth_error(FALSE, EACCES);
            dst = va_arg(ap, void **);
            *dst = a->a_tid->start_arg;
            break;
        }
        case PTH_ATTR_STATE: {
            pth_state_t *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            if (a->a_tid == NULL)
                return pth_error(FALSE, EACCES);
            dst = va_arg(ap, pth_state_t *);
            *dst = a->a_tid->state;
            break;
        }
        case PTH_ATTR_EVENTS: {
            pth_event_t *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            if (a->a_tid == NULL)
                return pth_error(FALSE, EACCES);
            dst = va_arg(ap, pth_event_t *);
            *dst = a->a_tid->events;
            break;
        }
        case PTH_ATTR_BOUND: {
            int *dst;
            if (cmd == PTH_ATTR_SET)
                return pth_error(FALSE, EPERM);
            dst = va_arg(ap, int *);
            *dst = (a->a_tid != NULL ? TRUE : FALSE);
            break;
        }
        default:
            return pth_error(FALSE, EINVAL);
    }
    return TRUE;
}

