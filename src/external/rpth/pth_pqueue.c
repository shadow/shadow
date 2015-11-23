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
**  pth_pqueue.c: Pth thread priority queues
*/
                             /* ``Real hackers can write assembly
                                  code in any language''
                                                   -- Unknown */
#include "pth_p.h"

#if cpp

/* thread priority queue */
struct pth_pqueue_st {
    pth_t q_head;
    int   q_num;
};
typedef struct pth_pqueue_st pth_pqueue_t;

#endif /* cpp */

/* initialize a priority queue; O(1) */
intern void pth_pqueue_init(pth_pqueue_t *q)
{
    if (q != NULL) {
        q->q_head = NULL;
        q->q_num  = 0;
    }
    return;
}

/* insert thread into priority queue; O(n) */
intern void pth_pqueue_insert(pth_pqueue_t *q, int prio, pth_t t)
{
    pth_t c;
    int p;

    if (q == NULL)
        return;
    if (q->q_head == NULL || q->q_num == 0) {
        /* add as first element */
        t->q_prev = t;
        t->q_next = t;
        t->q_prio = prio;
        q->q_head = t;
    }
    else if (q->q_head->q_prio < prio) {
        /* add as new head of queue */
        t->q_prev = q->q_head->q_prev;
        t->q_next = q->q_head;
        t->q_prev->q_next = t;
        t->q_next->q_prev = t;
        t->q_prio = prio;
        t->q_next->q_prio = prio - t->q_next->q_prio;
        q->q_head = t;
    }
    else {
        /* insert after elements with greater or equal priority */
        c = q->q_head;
        p = c->q_prio;
        while ((p - c->q_next->q_prio) >= prio && c->q_next != q->q_head) {
            c = c->q_next;
            p -= c->q_prio;
        }
        t->q_prev = c;
        t->q_next = c->q_next;
        t->q_prev->q_next = t;
        t->q_next->q_prev = t;
        t->q_prio = p - prio;
        if (t->q_next != q->q_head)
            t->q_next->q_prio -= t->q_prio;
    }
    q->q_num++;
    return;
}

/* remove thread with maximum priority from priority queue; O(1) */
intern pth_t pth_pqueue_delmax(pth_pqueue_t *q)
{
    pth_t t;

    if (q == NULL)
        return NULL;
    if (q->q_head == NULL)
        t = NULL;
    else if (q->q_head->q_next == q->q_head) {
        /* remove the last element and make queue empty */
        t = q->q_head;
        t->q_next = NULL;
        t->q_prev = NULL;
        t->q_prio = 0;
        q->q_head = NULL;
        q->q_num  = 0;
    }
    else {
        /* remove head of queue */
        t = q->q_head;
        t->q_prev->q_next = t->q_next;
        t->q_next->q_prev = t->q_prev;
        t->q_next->q_prio = t->q_prio - t->q_next->q_prio;
        t->q_prio = 0;
        q->q_head = t->q_next;
        q->q_num--;
    }
    return t;
}

/* remove thread from priority queue; O(n) */
intern void pth_pqueue_delete(pth_pqueue_t *q, pth_t t)
{
    if (q == NULL)
        return;
    if (q->q_head == NULL)
        return;
    else if (q->q_head == t) {
        if (t->q_next == t) {
            /* remove the last element and make queue empty */
            t->q_next = NULL;
            t->q_prev = NULL;
            t->q_prio = 0;
            q->q_head = NULL;
            q->q_num  = 0;
        }
        else {
            /* remove head of queue */
            t->q_prev->q_next = t->q_next;
            t->q_next->q_prev = t->q_prev;
            t->q_next->q_prio = t->q_prio - t->q_next->q_prio;
            t->q_prio = 0;
            q->q_head = t->q_next;
            q->q_num--;
        }
    }
    else {
        t->q_prev->q_next = t->q_next;
        t->q_next->q_prev = t->q_prev;
        if (t->q_next != q->q_head)
            t->q_next->q_prio += t->q_prio;
        t->q_prio = 0;
        q->q_num--;
    }
    return;
}

/* determine priority required to favorite a thread; O(1) */
#if cpp
#define pth_pqueue_favorite_prio(q) \
    ((q)->q_head != NULL ? (q)->q_head->q_prio + 1 : PTH_PRIO_MAX)
#endif

/* move a thread inside queue to the top; O(n) */
intern int pth_pqueue_favorite(pth_pqueue_t *q, pth_t t)
{
    if (q == NULL)
        return FALSE;
    if (q->q_head == NULL || q->q_num == 0)
        return FALSE;
    /* element is already at top */
    if (q->q_num == 1)
        return TRUE;
    /* move to top */
    pth_pqueue_delete(q, t);
    pth_pqueue_insert(q, pth_pqueue_favorite_prio(q), t);
    return TRUE;
}

/* increase priority of all(!) threads in queue; O(1) */
intern void pth_pqueue_increase(pth_pqueue_t *q)
{
    if (q == NULL)
        return;
    if (q->q_head == NULL)
        return;
    /* <grin> yes, that's all ;-) */
    q->q_head->q_prio += 1;
    return;
}

/* return number of elements in priority queue: O(1) */
#if cpp
#define pth_pqueue_elements(q) \
    ((q) == NULL ? (-1) : (q)->q_num)
#endif

/* walk to first thread in queue; O(1) */
#if cpp
#define pth_pqueue_head(q) \
    ((q) == NULL ? NULL : (q)->q_head)
#endif

/* walk to last thread in queue */
intern pth_t pth_pqueue_tail(pth_pqueue_t *q)
{
    if (q == NULL)
        return NULL;
    if (q->q_head == NULL)
        return NULL;
    return q->q_head->q_prev;
}

/* walk to next or previous thread in queue; O(1) */
intern pth_t pth_pqueue_walk(pth_pqueue_t *q, pth_t t, int direction)
{
    pth_t tn;

    if (q == NULL || t == NULL)
        return NULL;
    tn = NULL;
    if (direction == PTH_WALK_PREV) {
        if (t != q->q_head)
            tn = t->q_prev;
    }
    else if (direction == PTH_WALK_NEXT) {
        tn = t->q_next;
        if (tn == q->q_head)
            tn = NULL;
    }
    return tn;
}

/* check whether a thread is in a queue; O(n) */
intern int pth_pqueue_contains(pth_pqueue_t *q, pth_t t)
{
    pth_t tc;
    int found;

    found = FALSE;
    for (tc = pth_pqueue_head(q); tc != NULL;
         tc = pth_pqueue_walk(q, tc, PTH_WALK_NEXT)) {
        if (tc == t) {
            found = TRUE;
            break;
        }
    }
    return found;
}

