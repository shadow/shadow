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
**  pth_clean.c: Pth per-thread cleanup handler
*/
                             /* ``The concept seems to be clear by now.
                                  It has been defined several times
                                  by example of what it is not.''
                                                       -- Unknown */
#include "pth_p.h"

#if cpp

typedef struct pth_cleanup_st pth_cleanup_t;
struct pth_cleanup_st {
    pth_cleanup_t *next;
    void (*func)(void *);
    void *arg;
};

#endif /* cpp */

int pth_cleanup_push(void (*func)(void *), void *arg)
{
    pth_cleanup_t *cleanup;

    if (func == NULL)
        return pth_error(FALSE, EINVAL);
    if ((cleanup = (pth_cleanup_t *)malloc(sizeof(pth_cleanup_t))) == NULL)
        return pth_error(FALSE, ENOMEM);
    cleanup->func = func;
    cleanup->arg  = arg;
    cleanup->next = pth_current->cleanups;
    pth_current->cleanups = cleanup;
    return TRUE;
}

int pth_cleanup_pop(int execute)
{
    pth_cleanup_t *cleanup;
    int rc;

    rc = FALSE;
    if ((cleanup = pth_current->cleanups) != NULL) {
        pth_current->cleanups = cleanup->next;
        if (execute)
            cleanup->func(cleanup->arg);
        free(cleanup);
        rc = TRUE;
    }
    return rc;
}

intern void pth_cleanup_popall(pth_t t, int execute)
{
    pth_cleanup_t *cleanup;

    while ((cleanup = t->cleanups) != NULL) {
        t->cleanups = cleanup->next;
        if (execute)
            cleanup->func(cleanup->arg);
        free(cleanup);
    }
    return;
}

