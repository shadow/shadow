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
**  pth_uctx.c: Pth user-space context handling (stand-alone sub-API)
*/
                             /* ``It worries me however, to realize
                                how tough an ass-hole I have had to
                                be, in order to get to stick to the
                                principle of doing things right,
                                rather than "just hack it in".''
                                -- Poul-Henning Kamp <phk@FreeBSD.org> */
#include "pth_p.h"

/* user-space context structure */
struct pth_uctx_st {
    int         uc_stack_own; /* whether stack were allocated by us */
    char       *uc_stack_ptr; /* pointer to start address of stack area */
    size_t      uc_stack_len; /* size of stack area */
    int         uc_mctx_set;  /* whether uc_mctx is set */
    pth_mctx_t  uc_mctx;      /* saved underlying machine context */
};

/* create user-space context structure */
int
pth_uctx_create(
    pth_uctx_t *puctx)
{
    pth_uctx_t uctx;

    /* argument sanity checking */
    if (puctx == NULL)
        return pth_error(FALSE, EINVAL);

    /* allocate the context structure */
    if ((uctx = (pth_uctx_t)malloc(sizeof(struct pth_uctx_st))) == NULL)
        return pth_error(FALSE, errno);

    /* initialize the context structure */
    uctx->uc_stack_own = FALSE;
    uctx->uc_stack_ptr = NULL;
    uctx->uc_stack_len = 0;
    uctx->uc_mctx_set  = FALSE;
    memset((void *)&uctx->uc_mctx, 0, sizeof(pth_mctx_t));

    /* pass result to caller */
    *puctx = uctx;

    return TRUE;
}

/* trampoline context */
typedef struct {
    pth_mctx_t *mctx_parent;
    pth_uctx_t  uctx_this;
    pth_uctx_t  uctx_after;
    void      (*start_func)(void *);
    void       *start_arg;
} pth_uctx_trampoline_t;
pth_uctx_trampoline_t pth_uctx_trampoline_ctx;

/* trampoline function for pth_uctx_make() */
static void pth_uctx_trampoline(void)
{
    volatile pth_uctx_trampoline_t ctx;

    /* move context information from global to local storage */
    ctx.mctx_parent = pth_uctx_trampoline_ctx.mctx_parent;
    ctx.uctx_this   = pth_uctx_trampoline_ctx.uctx_this;
    ctx.uctx_after  = pth_uctx_trampoline_ctx.uctx_after;
    ctx.start_func  = pth_uctx_trampoline_ctx.start_func;
    ctx.start_arg   = pth_uctx_trampoline_ctx.start_arg;

    /* switch back to parent */
    pth_mctx_switch(&(ctx.uctx_this->uc_mctx), ctx.mctx_parent);

    /* enter start function */
    (*ctx.start_func)(ctx.start_arg);

    /* switch to successor user-space context */
    if (ctx.uctx_after != NULL)
        pth_mctx_restore(&(ctx.uctx_after->uc_mctx));

    /* terminate process (the only reasonable thing to do here) */
    exit(0);

    /* NOTREACHED */
    return;
}

/* make setup of user-space context structure */
int
pth_uctx_make(
    pth_uctx_t uctx,
    char *sk_addr, size_t sk_size,
    const sigset_t *sigmask,
    void (*start_func)(void *), void *start_arg,
    pth_uctx_t uctx_after)
{
    pth_mctx_t mctx_parent;
    sigset_t ss;

    /* argument sanity checking */
    if (uctx == NULL || start_func == NULL || sk_size < 16*1024)
        return pth_error(FALSE, EINVAL);

    /* configure run-time stack */
    if (sk_addr == NULL) {
        if ((sk_addr = (char *)malloc(sk_size)) == NULL)
            return pth_error(FALSE, errno);
        uctx->uc_stack_own = TRUE;
    }
    else
        uctx->uc_stack_own = FALSE;
    uctx->uc_stack_ptr = sk_addr;
    uctx->uc_stack_len = sk_size;

    /* configure the underlying machine context */
    if (!pth_mctx_set(&uctx->uc_mctx, pth_uctx_trampoline,
                      uctx->uc_stack_ptr, uctx->uc_stack_ptr+uctx->uc_stack_len))
        return pth_error(FALSE, errno);

    /* move context information into global storage for the trampoline jump */
    pth_uctx_trampoline_ctx.mctx_parent = &mctx_parent;
    pth_uctx_trampoline_ctx.uctx_this   = uctx;
    pth_uctx_trampoline_ctx.uctx_after  = uctx_after;
    pth_uctx_trampoline_ctx.start_func  = start_func;
    pth_uctx_trampoline_ctx.start_arg   = start_arg;

    /* optionally establish temporary signal mask */
    if (sigmask != NULL)
        sigprocmask(SIG_SETMASK, sigmask, &ss);

    /* perform the trampoline step */
    pth_mctx_switch(&mctx_parent, &(uctx->uc_mctx));

    /* optionally restore original signal mask */
    if (sigmask != NULL)
        sigprocmask(SIG_SETMASK, &ss, NULL);

    /* finally flag that the context is now configured */
    uctx->uc_mctx_set = TRUE;

    return TRUE;
}

/* switch from current to other user-space context */
int
pth_uctx_switch(
    pth_uctx_t uctx_from,
    pth_uctx_t uctx_to)
{
    /* argument sanity checking */
    if (uctx_from == NULL || uctx_to == NULL)
        return pth_error(FALSE, EINVAL);
    if (!(uctx_to->uc_mctx_set))
        return pth_error(FALSE, EPERM);

    /* switch underlying machine context */
    uctx_from->uc_mctx_set = TRUE;
    pth_mctx_switch(&(uctx_from->uc_mctx), &(uctx_to->uc_mctx));

    return TRUE;
}

/* destroy user-space context structure */
int
pth_uctx_destroy(
    pth_uctx_t uctx)
{
    /* argument sanity checking */
    if (uctx == NULL)
        return pth_error(FALSE, EINVAL);

    /* deallocate dynamically allocated stack */
    if (uctx->uc_stack_own && uctx->uc_stack_ptr != NULL)
        free(uctx->uc_stack_ptr);

    /* deallocate context structure */
    free(uctx);

    return TRUE;
}

