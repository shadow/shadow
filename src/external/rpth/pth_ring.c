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
**  pth_ring.c: Pth ring data structure
*/
                             /* ``Unix was not designed to stop people
                                  from doing stupid things, because that
                                  would also stop them from doing clever
                                  things.''         --Doug Gwyn          */

/*
 * This is a "ring" data structure, a special case of a list. It is
 * implemented through double-chained nodes. The link structure is part
 * of the nodes, i.e. no extra memory is required for the ring itself
 * and the ring can contain as many nodes as fit into memory. The main
 * advantage of using a ring instead of a plain list is to make the ring
 * operations easier (less special cases!). The ring is usually used
 * in Pth to represent a "set" of something. All operations are O(1),
 * except for the check whether a node is part of the ring (which is
 * O(N)).
 */

#include "pth_p.h"

/* initialize ring; O(1) */
intern void pth_ring_init(pth_ring_t *r)
{
    if (r == NULL)
        return;
    r->r_hook  = NULL;
    r->r_nodes = 0;
    return;
}

/* return number of nodes in ring; O(1) */
#if cpp
#define pth_ring_elements(r) \
    ((r) == NULL ? (-1) : (r)->r_nodes)
#endif

/* return first node in ring; O(1) */
#if cpp
#define pth_ring_first(r) \
    ((r) == NULL ? NULL : (r)->r_hook)
#endif

/* return last node in ring; O(1) */
#if cpp
#define pth_ring_last(r) \
    ((r) == NULL ? NULL : ((r)->r_hook == NULL ? NULL : (r)->r_hook->rn_prev))
#endif

/* walk to next node in ring; O(1) */
#if cpp
#define pth_ring_next(r, rn) \
    (((r) == NULL || (rn) == NULL) ? NULL : ((rn)->rn_next == (r)->r_hook ? NULL : (rn)->rn_next))
#endif

/* walk to previous node in ring; O(1) */
#if cpp
#define pth_ring_prev(r, rn) \
    (((r) == NULL || (rn) == NULL) ? NULL : ((rn)->rn_prev == (r)->r_hook->rn_prev ? NULL : (rn)->rn_prev))
#endif

/* insert node into ring; O(1) */
#if cpp
#define pth_ring_insert(r, rn) \
    pth_ring_append((r), (rn))
#endif

/* insert node after a second node in ring; O(1) */
intern void pth_ring_insert_after(pth_ring_t *r, pth_ringnode_t *rn1, pth_ringnode_t *rn2)
{
    if (r == NULL || rn1 == NULL || rn2 == NULL)
        return;
    rn2->rn_prev = rn1;
    rn2->rn_next = rn1->rn_next;
    rn2->rn_prev->rn_next = rn2;
    rn2->rn_next->rn_prev = rn2;
    r->r_nodes++;
    return;
}

/* insert node before a second node in ring; O(1) */
intern void pth_ring_insert_before(pth_ring_t *r, pth_ringnode_t *rn1, pth_ringnode_t *rn2)
{
    if (r == NULL || rn1 == NULL || rn2 == NULL)
        return;
    rn2->rn_next = rn1;
    rn2->rn_prev = rn1->rn_prev;
    rn2->rn_prev->rn_next = rn2;
    rn2->rn_next->rn_prev = rn2;
    r->r_nodes++;
    return;
}

/* delete an node from ring; O(1) */
intern void pth_ring_delete(pth_ring_t *r, pth_ringnode_t *rn)
{
    if (r == NULL || rn == NULL)
        return;
    if (r->r_hook == rn && rn->rn_prev == rn && rn->rn_next == rn)
        r->r_hook = NULL;
    else {
        if (r->r_hook == rn)
            r->r_hook = rn->rn_next;
        rn->rn_prev->rn_next = rn->rn_next;
        rn->rn_next->rn_prev = rn->rn_prev;
    }
    r->r_nodes--;
    return;
}

/* prepend an node to ring; O(1) */
intern void pth_ring_prepend(pth_ring_t *r, pth_ringnode_t *rn)
{
    if (r == NULL || rn == NULL)
        return;
    if (r->r_hook == NULL) {
        r->r_hook = rn;
        rn->rn_next = rn;
        rn->rn_prev = rn;
    }
    else {
        rn->rn_next = r->r_hook;
        rn->rn_prev = r->r_hook->rn_prev;
        rn->rn_next->rn_prev = rn;
        rn->rn_prev->rn_next = rn;
        r->r_hook = rn;
    }
    r->r_nodes++;
    return;
}

/* append an node to ring; O(1) */
intern void pth_ring_append(pth_ring_t *r, pth_ringnode_t *rn)
{
    if (r == NULL || rn == NULL)
        return;
    if (r->r_hook == NULL) {
        r->r_hook = rn;
        rn->rn_next = rn;
        rn->rn_prev = rn;
    }
    else {
        rn->rn_next = r->r_hook;
        rn->rn_prev = r->r_hook->rn_prev;
        rn->rn_next->rn_prev = rn;
        rn->rn_prev->rn_next = rn;
    }
    r->r_nodes++;
    return;
}

/* treat ring as stack: push node onto stack; O(1) */
#if cpp
#define pth_ring_push(r, rn) \
    pth_ring_prepend((r), (rn))
#endif

/* treat ring as stack: pop node from stack; O(1) */
intern pth_ringnode_t *pth_ring_pop(pth_ring_t *r)
{
    pth_ringnode_t *rn;

    rn = pth_ring_first(r);
    if (rn != NULL)
        pth_ring_delete(r, rn);
    return rn;
}

/* treat ring as queue: favorite a node in the ring; O(1) */
intern int pth_ring_favorite(pth_ring_t *r, pth_ringnode_t *rn)
{
    if (r == NULL)
        return FALSE;
    if (r->r_hook == NULL)
        return FALSE;
    /* element is perhaps already at ring hook */
    if (r->r_hook == rn)
        return TRUE;
    /* move to hook of ring */
    pth_ring_delete(r, rn);
    pth_ring_prepend(r, rn);
    return TRUE;
}

/* treat ring as queue: enqueue node; O(1) */
#if cpp
#define pth_ring_enqueue(r, rn) \
    pth_ring_prepend((r), (rn))
#endif

/* treat ring as queue: dequeue node; O(1) */
intern pth_ringnode_t *pth_ring_dequeue(pth_ring_t *r)
{
    pth_ringnode_t *rn;

    rn = pth_ring_last(r);
    if (rn != NULL)
        pth_ring_delete(r, rn);
    return rn;
}

/* check whether node is contained in ring; O(n) */
intern int pth_ring_contains(pth_ring_t *r, pth_ringnode_t *rns)
{
    pth_ringnode_t *rn;
    int rc;

    if (r == NULL || rns == NULL)
        return pth_error(FALSE, EINVAL);
    rc = FALSE;
    rn = r->r_hook;
    if (rn != NULL) {
        do {
            if (rn == rns) {
                rc = TRUE;
                break;
            }
            rn = rn->rn_next;
        } while (rn != r->r_hook);
    }
    return rc;
}

