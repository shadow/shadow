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
**  test_std.c: Pth standard test program
*/
                             /* ``Understanding a problem is knowing why
                                it is hard to solve it, and why the most
                                straightforward approaches won't work.''
                                                  -- Karl Popper        */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "pth.h"

#define FAILED_IF(expr) \
     if (expr) { \
         fprintf(stderr, "*** ERROR, TEST FAILED:\n*** errno=%d\n\n", errno); \
         exit(1); \
     }

static void *t1_func(void *arg)
{
    int i;
    long val;

    val = (long)arg;
    FAILED_IF(val != 123)
    for (i = 0; i < 100; i++) {
        val += 10;
        pth_yield(NULL);
    }
    return (void *)val;
}

static void *t2_func(void *arg)
{
    long val;
    pth_t tid;
    void *rval;
    int rc;

    val = (long)arg;
    if (val < 9) {
        val++;
        fprintf(stderr, "Spawning thread %ld\n", val);
        tid = pth_spawn(PTH_ATTR_DEFAULT, t2_func, (void *)(val));
        FAILED_IF(tid == NULL)
        rc = pth_join(tid, &rval);
        fprintf(stderr, "Joined thread %ld\n", val);
        FAILED_IF(rc == FALSE)
        rval = (void *)((long)arg * (long)rval);
    }
    else
        rval = arg;
    return rval;
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "\n=== TESTING GLOBAL LIBRARY API ===\n\n");
    {
        int version;

        fprintf(stderr, "Fetching library version\n");
        version = pth_version();
        FAILED_IF(version == 0x0)
        fprintf(stderr, "version = 0x%X\n", version);
    }

    fprintf(stderr, "\n=== TESTING BASIC OPERATION ===\n\n");
    {
        int rc;

        fprintf(stderr, "Initializing Pth system (spawns scheduler and main thread)\n");
        rc = pth_init();
        FAILED_IF(rc == FALSE)
        fprintf(stderr, "Killing Pth system for testing purposes\n");
        pth_kill();
        fprintf(stderr, "Re-Initializing Pth system\n");
        rc = pth_init();
        FAILED_IF(rc == FALSE)
    }

    fprintf(stderr, "\n=== TESTING BASIC THREAD OPERATION ===\n\n");
    {
        pth_attr_t attr;
        pth_t tid;
        void *val;
        int rc;

        fprintf(stderr, "Creating attribute object\n");
        attr = pth_attr_new();
        FAILED_IF(attr == NULL)
        rc = pth_attr_set(attr, PTH_ATTR_NAME, "test1");
        FAILED_IF(rc == FALSE)
        rc = pth_attr_set(attr, PTH_ATTR_PRIO, PTH_PRIO_MAX);
        FAILED_IF(rc == FALSE)

        fprintf(stderr, "Spawning thread\n");
        tid = pth_spawn(attr, t1_func, (void *)(123));
        FAILED_IF(tid == NULL)
        pth_attr_destroy(attr);

        fprintf(stderr, "Joining thread\n");
        rc = pth_join(tid, &val);
        FAILED_IF(rc == FALSE)
        FAILED_IF(val != (void *)(1123))
    }

    fprintf(stderr, "\n=== TESTING NESTED THREAD OPERATION ===\n\n");
    {
        pth_t tid;
        void *val;
        int rc;

        fprintf(stderr, "Spawning thread 1\n");
        tid = pth_spawn(PTH_ATTR_DEFAULT, t2_func, (void *)(1));
        FAILED_IF(tid == NULL)

        rc = pth_join(tid, &val);
        fprintf(stderr, "Joined thread 1\n");
        FAILED_IF(rc == FALSE)
        FAILED_IF(val != (void *)(1*2*3*4*5*6*7*8*9))
    }

    pth_kill();
    fprintf(stderr, "\nOK - ALL TESTS SUCCESSFULLY PASSED.\n\n");
    exit(0);
}

