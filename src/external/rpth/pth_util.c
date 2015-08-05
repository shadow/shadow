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
**  pth_util.c: Pth utility functions
*/
                             /* ``Premature optimization is
                                  the root of all evil.''
                                             -- D.E.Knuth */
#include "pth_p.h"

/* calculate numerical mimimum */
#if cpp
#define pth_util_min(a,b) \
        ((a) > (b) ? (b) : (a))
#endif

/* delete a pending signal */
static void pth_util_sigdelete_sighandler(int _sig)
{
    /* nop */
    return;
}
intern int pth_util_sigdelete(int sig)
{
    sigset_t ss, oss;
    struct sigaction sa, osa;

    /* check status of signal */
    sigpending(&ss);
    if (!sigismember(&ss, sig))
        return FALSE;

    /* block signal and remember old mask */
    sigemptyset(&ss);
    sigaddset(&ss, sig);
    pth_sc(sigprocmask)(SIG_BLOCK, &ss, &oss);

    /* set signal action to our dummy handler */
    sa.sa_handler = pth_util_sigdelete_sighandler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(sig, &sa, &osa) != 0) {
        pth_sc(sigprocmask)(SIG_SETMASK, &oss, NULL);
        return FALSE;
    }

    /* now let signal be delivered */
    sigfillset(&ss);
    sigdelset(&ss, sig);
    sigsuspend(&ss);

    /* restore signal mask and handler */
    sigaction(sig, &osa, NULL);
    pth_sc(sigprocmask)(SIG_SETMASK, &oss, NULL);
    return TRUE;
}

/* copy a string like strncpy() but always null-terminate */
intern char *pth_util_cpystrn(char *dst, const char *src, size_t dst_size)
{
    register char *d, *end;

    if (dst_size == 0)
        return dst;
    d = dst;
    end = dst + dst_size - 1;
    for (; d < end; ++d, ++src) {
        if ((*d = *src) == '\0')
            return d;
    }
    *d = '\0';
    return d;
}

/* check whether a file-descriptor is valid */
intern int pth_util_fd_valid(int fd)
{
    if (fd < 0 || fd >= FD_SETSIZE)
        return FALSE;
    if (fcntl(fd, F_GETFL) == -1 && errno == EBADF)
        return FALSE;
    return TRUE;
}

/* merge input fd set into output fds */
intern void pth_util_fds_merge(int nfd,
                               fd_set *ifds1, fd_set *ofds1,
                               fd_set *ifds2, fd_set *ofds2,
                               fd_set *ifds3, fd_set *ofds3)
{
    register int s;

    for (s = 0; s < nfd; s++) {
        if (ifds1 != NULL)
            if (FD_ISSET(s, ifds1))
                FD_SET(s, ofds1);
        if (ifds2 != NULL)
            if (FD_ISSET(s, ifds2))
                FD_SET(s, ofds2);
        if (ifds3 != NULL)
            if (FD_ISSET(s, ifds3))
                FD_SET(s, ofds3);
    }
    return;
}

/* test whether fds in the input fd sets occurred in the output fds */
intern int pth_util_fds_test(int nfd,
                             fd_set *ifds1, fd_set *ofds1,
                             fd_set *ifds2, fd_set *ofds2,
                             fd_set *ifds3, fd_set *ofds3)
{
    register int s;

    for (s = 0; s < nfd; s++) {
        if (ifds1 != NULL)
            if (FD_ISSET(s, ifds1) && FD_ISSET(s, ofds1))
                return TRUE;
        if (ifds2 != NULL)
            if (FD_ISSET(s, ifds2) && FD_ISSET(s, ofds2))
                return TRUE;
        if (ifds3 != NULL)
            if (FD_ISSET(s, ifds3) && FD_ISSET(s, ofds3))
                return TRUE;
    }
    return FALSE;
}

/*
 * clear fds in input fd sets if not occurred in output fd sets and return
 * number of remaining input fds. This number uses BSD select(2) semantics: a
 * fd in two set counts twice!
 */
intern int pth_util_fds_select(int nfd,
                               fd_set *ifds1, fd_set *ofds1,
                               fd_set *ifds2, fd_set *ofds2,
                               fd_set *ifds3, fd_set *ofds3)
{
    register int s;
    register int n;

    n = 0;
    for (s = 0; s < nfd; s++) {
        if (ifds1 != NULL && FD_ISSET(s, ifds1)) {
            if (!FD_ISSET(s, ofds1))
               FD_CLR(s, ifds1);
            else
               n++;
        }
        if (ifds2 != NULL && FD_ISSET(s, ifds2)) {
            if (!FD_ISSET(s, ofds2))
                FD_CLR(s, ifds2);
            else
                n++;
        }
        if (ifds3 != NULL && FD_ISSET(s, ifds3)) {
            if (!FD_ISSET(s, ofds3))
                FD_CLR(s, ifds3);
            else
                n++;
        }
    }
    return n;
}

