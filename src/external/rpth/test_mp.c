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
**  test_mp.c: Pth test program (message port handling)
*/
                             /* ``Failure is not an option.
                                It comes bundled with software.'' */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "pth.h"

#include "test_common.h"

/* our simple query structure */
struct query {
    pth_message_t head; /* the standard message header */
    char *string;       /* the query ingredients */
};

/* our worker thread which translates the string to upper case */
typedef struct {
    pth_msgport_t mp;
    pth_event_t ev;
} worker_cleanup_t;
static void worker_cleanup(void *arg)
{
    worker_cleanup_t *wc = (worker_cleanup_t *)arg;
    pth_event_free(wc->ev, PTH_FREE_THIS);
    pth_msgport_destroy(wc->mp);
    return;
}
static void *worker(void *_dummy)
{
    worker_cleanup_t wc;
    pth_msgport_t mp;
    pth_event_t ev;
    struct query *q;
    int i;

    fprintf(stderr, "worker: start\n");
    wc.mp = mp = pth_msgport_create("worker");
    wc.ev = ev = pth_event(PTH_EVENT_MSG, mp);
    pth_cleanup_push(worker_cleanup, &wc);
    for (;;) {
         if ((i = pth_wait(ev)) != 1)
             continue;
         while ((q = (struct query *)pth_msgport_get(mp)) != NULL) {
             fprintf(stderr, "worker: recv query <%s>\n", q->string);
             for (i = 0; q->string[i] != NUL; i++)
                 q->string[i] = toupper(q->string[i]);
             fprintf(stderr, "worker: send reply <%s>\n", q->string);
             pth_msgport_reply((pth_message_t *)q);
         }
    }
    return NULL;
}

/* a useless ticker thread */
static void *ticker(void *_arg)
{
    time_t now;
    fprintf(stderr, "ticker: start\n");
    for (;;) {
        pth_sleep(5);
        now = time(NULL);
        fprintf(stderr, "ticker was woken up on %s", ctime(&now));
    }
    /* NOTREACHED */
    return NULL;
}

#define MAXLINELEN 1024

int main(int argc, char *argv[])
{
    char caLine[MAXLINELEN];
    pth_event_t ev = NULL;
    pth_event_t evt = NULL;
    pth_t t_worker = NULL;
    pth_t t_ticker = NULL;
    pth_attr_t t_attr;
    pth_msgport_t mp = NULL;
    pth_msgport_t mp_worker = NULL;
    struct query *q = NULL;
    int n;

    if (!pth_init()) {
        perror("pth_init");
        exit(1);
    }

    fprintf(stderr, "This is TEST_MP, a Pth test using message ports.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Lines on stdin are send to a worker thread via message\n");
    fprintf(stderr, "ports, translated to upper case by the worker thread and\n");
    fprintf(stderr, "send back to the main thread via message ports.\n");
    fprintf(stderr, "Additionally a useless ticker thread awakens every 5s.\n");
    fprintf(stderr, "Enter \"quit\" on stdin for stopping this test.\n");
    fprintf(stderr, "\n");

    t_attr = pth_attr_new();
    pth_attr_set(t_attr, PTH_ATTR_NAME, "worker");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE, TRUE);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE, 16*1024);
    t_worker = pth_spawn(t_attr, worker, NULL);
    pth_attr_set(t_attr, PTH_ATTR_NAME, "ticker");
    t_ticker = pth_spawn(t_attr, ticker, NULL);
    pth_attr_destroy(t_attr);
    pth_yield(NULL);

    mp_worker = pth_msgport_find("worker");
    mp = pth_msgport_create("main");
    q = (struct query *)malloc(sizeof(struct query));
    ev = pth_event(PTH_EVENT_MSG, mp);

    evt = NULL;
    for (;;) {
        if (evt == NULL)
            evt = pth_event(PTH_EVENT_TIME, pth_timeout(20,0));
        else
            evt = pth_event(PTH_EVENT_TIME|PTH_MODE_REUSE, evt, pth_timeout(20,0));
        n = pth_readline_ev(STDIN_FILENO, caLine, MAXLINELEN, evt);
        if (n == -1 && pth_event_status(evt) == PTH_STATUS_OCCURRED) {
            fprintf(stderr, "main: Hey, what are you waiting for? Type in something!\n");
            continue;
        }
        if (n < 0) {
            fprintf(stderr, "main: I/O read error on stdin\n");
            break;
        }
        if (n == 0) {
            fprintf(stderr, "main: EOF on stdin\n");
            break;
        }
        caLine[n-1] = NUL;
        if (strcmp(caLine, "quit") == 0) {
            fprintf(stderr, "main: quit\n");
            break;
        }
        fprintf(stderr, "main: out --> <%s>\n", caLine);
        q->string = caLine;
        q->head.m_replyport = mp;
        pth_msgport_put(mp_worker, (pth_message_t *)q);
        pth_wait(ev);
        q = (struct query *)pth_msgport_get(mp);
        fprintf(stderr, "main: in <-- <%s>\n", q->string);
    }

    free(q);
    pth_event_free(ev, PTH_FREE_THIS);
    pth_event_free(evt, PTH_FREE_THIS);
    pth_msgport_destroy(mp);
    pth_cancel(t_worker);
    pth_join(t_worker, NULL);
    pth_cancel(t_ticker);
    pth_join(t_ticker, NULL);
    pth_kill();
    return 0;
}

