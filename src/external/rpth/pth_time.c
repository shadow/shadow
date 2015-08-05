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
**  pth_time.c: Pth time calculations
*/
                             /* ``Real programmers confuse
                                  Christmas and Halloween
                                  because DEC 25 = OCT 31.''
                                             -- Unknown     */
#include "pth_p.h"

#if cpp
#define PTH_TIME_NOW  (pth_time_t *)(0)
#define PTH_TIME_ZERO &pth_time_zero
#define PTH_TIME(sec,usec) { sec, usec }
#define pth_time_equal(t1,t2) \
        (((t1).tv_sec == (t2).tv_sec) && ((t1).tv_usec == (t2).tv_usec))
#endif /* cpp */

/* a global variable holding a zero time */
intern pth_time_t pth_time_zero = { 0L, 0L };

/* sleep for a specified amount of microseconds */
intern void pth_time_usleep(unsigned long usec)
{
#ifdef HAVE_USLEEP
    usleep((unsigned int )usec);
#else
    struct timeval timeout;
    timeout.tv_sec  = usec / 1000000;
    timeout.tv_usec = usec - (1000000 * timeout.tv_sec);
    while (pth_sc(select)(1, NULL, NULL, NULL, &timeout) < 0 && errno == EINTR) ;
#endif
    return;
}

/* calculate: t1 = t2 */
#if cpp
#if defined(HAVE_GETTIMEOFDAY_ARGS1)
#define __gettimeofday(t) gettimeofday(t)
#else
#define __gettimeofday(t) gettimeofday(t, NULL)
#endif
#define pth_time_set(t1,t2) \
    do { \
        if ((t2) == PTH_TIME_NOW) \
            __gettimeofday((t1)); \
        else { \
            (t1)->tv_sec  = (t2)->tv_sec; \
            (t1)->tv_usec = (t2)->tv_usec; \
        } \
    } while (0)
#endif /* cpp */

/* time value constructor */
pth_time_t pth_time(long sec, long usec)
{
    pth_time_t tv;

    tv.tv_sec  = sec;
    tv.tv_usec = usec;
    return tv;
}

/* timeout value constructor */
pth_time_t pth_timeout(long sec, long usec)
{
    pth_time_t tv;
    pth_time_t tvd;

    pth_time_set(&tv, PTH_TIME_NOW);
    tvd.tv_sec  = sec;
    tvd.tv_usec = usec;
    pth_time_add(&tv, &tvd);
    return tv;
}

/* calculate: t1 <=> t2 */
intern int pth_time_cmp(pth_time_t *t1, pth_time_t *t2)
{
    int rc;

    rc = t1->tv_sec - t2->tv_sec;
    if (rc == 0)
         rc = t1->tv_usec - t2->tv_usec;
    return rc;
}

/* calculate: t1 = t1 + t2 */
#if cpp
#define pth_time_add(t1,t2) \
    (t1)->tv_sec  += (t2)->tv_sec; \
    (t1)->tv_usec += (t2)->tv_usec; \
    if ((t1)->tv_usec > 1000000) { \
        (t1)->tv_sec  += 1; \
        (t1)->tv_usec -= 1000000; \
    }
#endif

/* calculate: t1 = t1 - t2 */
#if cpp
#define pth_time_sub(t1,t2) \
    (t1)->tv_sec  -= (t2)->tv_sec; \
    (t1)->tv_usec -= (t2)->tv_usec; \
    if ((t1)->tv_usec < 0) { \
        (t1)->tv_sec  -= 1; \
        (t1)->tv_usec += 1000000; \
    }
#endif

/* calculate: t1 = t1 / n */
intern void pth_time_div(pth_time_t *t1, int n)
{
    long q, r;

    q = (t1->tv_sec / n);
    r = (((t1->tv_sec % n) * 1000000) / n) + (t1->tv_usec / n);
    if (r > 1000000) {
        q += 1;
        r -= 1000000;
    }
    t1->tv_sec  = q;
    t1->tv_usec = r;
    return;
}

/* calculate: t1 = t1 * n */
intern void pth_time_mul(pth_time_t *t1, int n)
{
    t1->tv_sec  *= n;
    t1->tv_usec *= n;
    t1->tv_sec  += (t1->tv_usec / 1000000);
    t1->tv_usec  = (t1->tv_usec % 1000000);
    return;
}

/* convert a time structure into a double value */
intern double pth_time_t2d(pth_time_t *t)
{
    double d;

    d = ((double)t->tv_sec*1000000 + (double)t->tv_usec) / 1000000;
    return d;
}

/* convert a time structure into a integer value */
intern int pth_time_t2i(pth_time_t *t)
{
    int i;

    i = (t->tv_sec*1000000 + t->tv_usec) / 1000000;
    return i;
}

/* check whether time is positive */
intern int pth_time_pos(pth_time_t *t)
{
    if (t->tv_sec > 0 && t->tv_usec > 0)
        return 1;
    else
        return 0;
}

