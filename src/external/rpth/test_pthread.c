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
**  test_pthread.c: Pth test program (pthread API)
*/
                             /* ``You can check out any time you
                                like, but you can never leave.''
                                -- The Eagles, Hotel California */
#ifdef GLOBAL
#include <pthread.h>
#else
#include "pthread.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define die(str) \
    do { \
        fprintf(stderr, "**die: %s: errno=%d\n", str, errno); \
        exit(1); \
    } while (0)

static void *child(void *_arg)
{
    char *name = (char *)_arg;
    int i;

    fprintf(stderr, "child: startup %s\n", name);
    for (i = 0; i < 100; i++) {
        if (i++ % 10 == 0)
            sleep(1);
        fprintf(stderr, "child: %s counts i=%d\n", name, i);
    }
    fprintf(stderr, "child: shutdown %s\n", name);
    return _arg;
}

int main(int argc, char *argv[])
{
    pthread_attr_t thread_attr;
    pthread_t thread[4];
    char *rc;

    fprintf(stderr, "main: init\n");

    fprintf(stderr, "main: initializing attribute object\n");
    if (pthread_attr_init(&thread_attr) != 0)
        die("pthread_attr_init");
    if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE) != 0)
        die("pthread_attr_setdetachstate");

    fprintf(stderr, "main: create thread 1\n");
    if (pthread_create(&thread[0], &thread_attr, child, (void *)"foo") != 0)
        die("pthread_create");
    fprintf(stderr, "main: create thread 2\n");
    if (pthread_create(&thread[1], &thread_attr, child, (void *)"bar") != 0)
        die("pthread_create");
    fprintf(stderr, "main: create thread 3\n");
    if (pthread_create(&thread[2], &thread_attr, child, (void *)"baz") != 0)
        die("pthread_create");
    fprintf(stderr, "main: create thread 4\n");
    if (pthread_create(&thread[3], &thread_attr, child, (void *)"quux") != 0)
        die("pthread_create");

    fprintf(stderr, "main: destroying attribute object\n");
    if (pthread_attr_destroy(&thread_attr) != 0)
        die("pthread_attr_destroy");

    sleep(3);

    fprintf(stderr, "main: joining...\n");
    if (pthread_join(thread[0], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stderr, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[1], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stderr, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[2], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stderr, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[3], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stderr, "main: joined thread: %s\n", rc);

    fprintf(stderr, "main: exit\n");
    return 0;
}

