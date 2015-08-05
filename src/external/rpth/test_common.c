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
**  test_common.c: Pth common test program stuff
*/
                             /* ``It doesn't need to be tested,
                                  because it works.''
                                           -- Richard Holloway */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pth.h"

#include "test_common.h"

/*
 * implementation of a convinient greedy tread-safe line reading function to
 * avoid slow byte-wise reading from filedescriptors - which is important for
 * high-performance situations.
 */

#define READLINE_MAXLEN 1024
static pth_key_t  readline_key;
static pth_once_t readline_once_ctrl = PTH_ONCE_INIT;

typedef struct {
    int   rl_cnt;
    char *rl_bufptr;
    char  rl_buf[READLINE_MAXLEN];
} readline_buf;

static void readline_buf_destroy(void *vp)
{
    free(vp);
    return;
}

static void readline_init(void *vp)
{
    pth_key_create(&readline_key, readline_buf_destroy);
    return;
}

ssize_t pth_readline(int fd, void *buf, size_t buflen)
{
    return pth_readline_ev(fd, buf, buflen, NULL);
}

ssize_t pth_readline_ev(int fd, void *buf, size_t buflen, pth_event_t ev_extra)
{
    size_t n;
    ssize_t rc;
    char c = '\0', *cp;
    readline_buf *rl;

    pth_once(&readline_once_ctrl, readline_init, NULL);
    if ((rl = (readline_buf *)pth_key_getdata(readline_key)) == NULL) {
        rl = (readline_buf *)malloc(sizeof(readline_buf));
        rl->rl_cnt = 0;
        rl->rl_bufptr = NULL;
        pth_key_setdata(readline_key, rl);
    }
    cp = (char *)buf;
    for (n = 1; n < buflen; n++) {

        /* fetch one character (but read more) */
        rc = 1;
        if (rl->rl_cnt <= 0) {
            if ((rl->rl_cnt = pth_read_ev(fd, rl->rl_buf, READLINE_MAXLEN, ev_extra)) < 0)
                rc = -1;
            else if (rl->rl_cnt == 0)
                rc = 0;
            else
                rl->rl_bufptr = rl->rl_buf;
        }
        if (rc == 1) {
            rl->rl_cnt--;
            c = *rl->rl_bufptr++;
        }

        /* act on fetched character */
        if (rc == 1) {
            if (c == '\r') {
                n--;
                continue;
            }
            *cp++ = c;
            if (c == '\n')
                break;
        }
        else if (rc == 0) {
            if (n == 1)
                return 0;
            else
                break;
        }
        else
            return -1;
    }
    *cp = NUL;
    return n;
}

