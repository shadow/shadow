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
**  test_sfio.c: Pth test program (Sfio support)
*/
                             /* ``No, I'm not going to explain it.
                                  If you can't figure it out, you
                                  didn't want to know anyway...''
                                            -- Larry Wall          */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pth.h"

#if PTH_EXT_SFIO

/* a worker thread */
static void *worker(void *_dummy)
{
    char line[1024];
    int c;
    int i;

    for (;;) {
        i = 0;
        while (!sfeof(sfstdin)) {
            c = sfgetc(sfstdin);
            if (c == '\n') {
                line[i] = '\0';
                break;
            }
            line[i++] = (char)c;
        }
        sfprintf(sfstderr, "you entered '%s' on sfstdin\n", line);
    }
    return NULL;
}

/* a useless ticker thread */
static void *ticker(void *_arg)
{
    time_t now;
    sfprintf(sfstderr, "ticker: start\n");
    for (;;) {
        pth_sleep(5);
        now = time(NULL);
        sfprintf(sfstderr, "ticker was woken up on %s", ctime(&now));
    }
    /* NOTREACHED */
    return NULL;
}

int main(int argc, char *argv[])
{
    Sfdisc_t *disc;
    pth_attr_t a;
    pth_t w;
    pth_t t;

    pth_init();

    /* get an Sfio discipline for Pth */
    if ((disc = pth_sfiodisc()) == NULL) {
        perror("pth_sfioiodc");
        exit(1);
    }

    /* push it onto Sfio's input/output streams */
    if (sfdisc(sfstdin, disc) != disc) {
        perror("sfdisc");
        exit(1);
    }
    if (sfdisc(sfstdout, disc) != disc) {
        perror("sfdisc");
        exit(1);
    }

    sfprintf(sfstderr, "This is TEST_SFIO, a Pth test using Sfio disciplines.\n");
    sfprintf(sfstderr, "\n");
    sfprintf(sfstderr, "Stdout/Stdin are connected to Sfio streams with a Pth\n");
    sfprintf(sfstderr, "discipline on top of the streams in order to use Pth's\n");
    sfprintf(sfstderr, "I/O operations. It demonstrates that the process this\n");
    sfprintf(sfstderr, "way does not block. Instead only one thread blocks.\n");
    sfprintf(sfstderr, "\n");

    a = pth_attr_new();
    pth_attr_set(a, PTH_ATTR_NAME, "worker");
    pth_attr_set(a, PTH_ATTR_JOINABLE, FALSE);
    pth_attr_set(a, PTH_ATTR_STACK_SIZE, 16*1024);
    w = pth_spawn(a, worker, NULL);
    pth_attr_set(a, PTH_ATTR_NAME, "ticker");
    t = pth_spawn(a, ticker, NULL);
    pth_attr_destroy(a);

    pth_exit(NULL);

    return 0;
}

#else

int main(int argc, char *argv[])
{
    fprintf(stderr, "You have to build Pth with --with-sfio to run this test!\n");
    return 0;
}

#endif

