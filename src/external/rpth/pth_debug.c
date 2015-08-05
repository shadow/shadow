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
**  pth_debug.c: Pth debugging support
*/
                             /* ``MY HACK: This universe.
                                  Just one little problem:
                                  core keeps dumping.'' 
                                              -- Unknown  */
#include "pth_p.h"

#if cpp

#ifndef PTH_DEBUG

#define pth_debug1(a1)                     /* NOP */
#define pth_debug2(a1, a2)                 /* NOP */
#define pth_debug3(a1, a2, a3)             /* NOP */
#define pth_debug4(a1, a2, a3, a4)         /* NOP */
#define pth_debug5(a1, a2, a3, a4, a5)     /* NOP */
#define pth_debug6(a1, a2, a3, a4, a5, a6) /* NOP */

#else

#define pth_debug1(a1)                     pth_debug(__FILE__, __LINE__, 1, a1)
#define pth_debug2(a1, a2)                 pth_debug(__FILE__, __LINE__, 2, a1, a2)
#define pth_debug3(a1, a2, a3)             pth_debug(__FILE__, __LINE__, 3, a1, a2, a3)
#define pth_debug4(a1, a2, a3, a4)         pth_debug(__FILE__, __LINE__, 4, a1, a2, a3, a4)
#define pth_debug5(a1, a2, a3, a4, a5)     pth_debug(__FILE__, __LINE__, 5, a1, a2, a3, a4, a5)
#define pth_debug6(a1, a2, a3, a4, a5, a6) pth_debug(__FILE__, __LINE__, 6, a1, a2, a3, a4, a5, a6)

#endif /* PTH_DEBUG */

#endif /* cpp */

intern void pth_debug(const char *file, int line, int argc, const char *fmt, ...)
{
    va_list ap;
    static char str[1024];
    size_t n;

    pth_shield {
        va_start(ap, fmt);
        if (file != NULL)
            pth_snprintf(str, sizeof(str), "%d:%s:%04d: ", (int)getpid(), file, line);
        else
            str[0] = NUL;
        n = strlen(str);
        if (argc == 1)
            pth_util_cpystrn(str+n, fmt, sizeof(str)-n);
        else
            pth_vsnprintf(str+n, sizeof(str)-n, fmt, ap);
        va_end(ap);
        n = strlen(str);
        str[n++] = '\n';
        pth_sc(write)(STDERR_FILENO, str, n);
    }
    return;
}

/* dump out a page to stderr summarizing the internal state of Pth */
intern void pth_dumpstate(FILE *fp)
{
    fprintf(fp, "+----------------------------------------------------------------------\n");
    fprintf(fp, "| Pth Version: %s\n", PTH_VERSION_STR);
    fprintf(fp, "| Load Average: %.2f\n", pth_loadval);
    pth_dumpqueue(fp, "NEW", &pth_NQ);
    pth_dumpqueue(fp, "READY", &pth_RQ);
    fprintf(fp, "| Thread Queue RUNNING:\n");
    fprintf(fp, "|   1. thread 0x%lx (\"%s\")\n",
            (unsigned long)pth_current, pth_current->name);
    pth_dumpqueue(fp, "WAITING", &pth_WQ);
    pth_dumpqueue(fp, "SUSPENDED", &pth_SQ);
    pth_dumpqueue(fp, "DEAD", &pth_DQ);
    fprintf(fp, "+----------------------------------------------------------------------\n");
    return;
}

intern void pth_dumpqueue(FILE *fp, const char *qn, pth_pqueue_t *q)
{
    pth_t t;
    int n;
    int i;

    fprintf(fp, "| Thread Queue %s:\n", qn);
    n = pth_pqueue_elements(q);
    if (n == 0)
        fprintf(fp, "|   no threads\n");
    i = 1;
    for (t = pth_pqueue_head(q); t != NULL; t = pth_pqueue_walk(q, t, PTH_WALK_NEXT)) {
        fprintf(fp, "|   %d. thread 0x%lx (\"%s\")\n", i++, (unsigned long)t, t->name);
    }
    return;
}

