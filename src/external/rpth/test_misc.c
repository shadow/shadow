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
**  test_misc.c: Pth test program (misc functions)
*/
                             /* ``Study it forever and you'll still wonder.
                                  Fly it once and you'll know.''
                                                 -- Henry Spencer  */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pth.h"

pth_mutex_t mutex = PTH_MUTEX_INIT;

static void *my_reader(void *_arg)
{
    char buf[3];
    int n;

    for (;;) {
        n = pth_read(STDIN_FILENO, buf, 1);
        if (n < 0) {
            fprintf(stderr, "reader: error\n");
            break;
        }
        if (n == 0) {
            fprintf(stderr, "reader: EOF\n");
            break;
        }
        if (n == 1 && buf[0] == '\n') {
            buf[0] = '\\';
            buf[1] = 'n';
            n = 2;
        }
        buf[n] = NUL;
        fprintf(stderr, "reader: bytes=%d, char='%s'\n", n, buf);
        if (buf[0] == 'Q' || buf[0] == 'q')
            break;
        if (buf[0] == 'L' || buf[0] == 'l') {
            fprintf(stderr, "reader: ACQUIRE MUTEX\n");
            pth_mutex_acquire(&mutex, FALSE, NULL);
        }
        if (buf[0] == 'U' || buf[0] == 'u') {
            fprintf(stderr, "reader: RELEASE MUTEX\n");
            pth_mutex_release(&mutex);
        }
        fflush(stderr);
    }
    return NULL;
}

static void *my_child(void *_arg)
{
    int i;
    char *name = (char *)_arg;

    for (i = 0; i < 10; ++i) {
         pth_mutex_acquire(&mutex, FALSE, NULL);
         fprintf(stderr, "%s: %d\n", name, i);
         pth_mutex_release(&mutex);
         pth_usleep(500000);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    pth_t child[10];
    pth_attr_t t_attr;
    pth_attr_t t_attr2;
    long n;

    pth_init();

    fprintf(stderr, "This is TEST_MISC, a Pth test using various stuff.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "A stdin reader child and various looping childs are\n");
    fprintf(stderr, "spawned. When you enter 'l' you can lock a mutex which\n");
    fprintf(stderr, "blocks the looping childs. 'u' unlocks this mutex.\n");
    fprintf(stderr, "Enter 'q' to quit.\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Main Startup (%ld total threads running)\n", pth_ctrl(PTH_CTRL_GETTHREADS));

    t_attr = pth_attr_new();
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE, FALSE);
    pth_attr_set(t_attr, PTH_ATTR_NAME, "foo");
    child[0] = pth_spawn(t_attr, my_child, (void *)"foo");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "bar");
    child[1] = pth_spawn(t_attr, my_child, (void *)"bar");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "baz");
    child[2] = pth_spawn(t_attr, my_child, (void *)"baz");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "quux");
    child[3] = pth_spawn(t_attr, my_child, (void *)"quux");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "killer");
    pth_attr_set(t_attr, PTH_ATTR_PRIO, 4);
    child[4] = pth_spawn(t_attr, my_child, (void *)"killer");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "killer II");
    pth_attr_set(t_attr, PTH_ATTR_PRIO, 5);
    child[5] = pth_spawn(t_attr, my_child, (void *)"killer II");
    pth_attr_set(t_attr, PTH_ATTR_NAME, "reader");
    pth_attr_set(t_attr, PTH_ATTR_PRIO, PTH_PRIO_STD);
    child[6] = pth_spawn(t_attr, my_reader, (void *)"reader");
    pth_attr_destroy(t_attr);

    t_attr2 = pth_attr_of(child[0]);
    pth_attr_set(t_attr2, PTH_ATTR_PRIO, -1);
    pth_attr_destroy(t_attr2);
    t_attr2 = pth_attr_of(child[3]);
    pth_attr_set(t_attr2, PTH_ATTR_PRIO, +1);
    pth_attr_destroy(t_attr2);

    fprintf(stderr, "Main Loop (%ld total threads running)\n", pth_ctrl(PTH_CTRL_GETTHREADS));

    while ((n = pth_ctrl(PTH_CTRL_GETTHREADS)) > 1) {
         fprintf(stderr, "Main Loop (%ld total threads still running)\n", n);
         pth_usleep(500000);
    }
    fprintf(stderr, "Main Exit (%ld total threads running)\n", pth_ctrl(PTH_CTRL_GETTHREADS));

    pth_kill();
    return 0;
}

