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
**  pth_high.c: Pth high-level replacement functions
*/
                             /* ``The difference between a computer
                                  industry job and open-source software
                                  hacking is about 30 hours a week.''
                                         -- Ralf S. Engelschall     */

/*
 *  These functions used by the applications instead of the
 *  regular Unix/POSIX functions. When the regular functions would
 *  block, these variants let only the thread sleep.
 */

#include "pth_p.h"

/* Pth variant of nanosleep(2) */
int pth_nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    pth_time_t until;
    pth_time_t offset;
    pth_time_t now;
    pth_event_t ev;

    /* consistency checks for POSIX conformance */
    if (rqtp == NULL)
        return pth_error(-1, EFAULT);
    if (rqtp->tv_nsec < 0 || rqtp->tv_nsec > (1000*1000000))
        return pth_error(-1, EINVAL);

    /* short-circuit */
    if (rqtp->tv_sec == 0 && rqtp->tv_nsec == 0)
        return 0;

    /* calculate asleep time */
    offset = pth_time((long)(rqtp->tv_sec), (long)(rqtp->tv_nsec) / 1000);
    pth_time_set(&until, PTH_TIME_NOW);
    pth_time_add(&until, &offset);

    /* and let thread sleep until this time is elapsed */
    ev = pth_event(PTH_EVENT_TIME, until);
    if (ev == NULL)
        return pth_error(-1, errno);

    pth_wait(ev);

    pth_event_free(ev, PTH_FREE_THIS);

    /* optionally provide amount of slept time */
    if (rmtp != NULL) {
        pth_time_set(&now, PTH_TIME_NOW);
        pth_time_sub(&until, &now);
        rmtp->tv_sec  = until.tv_sec;
        rmtp->tv_nsec = until.tv_usec * 1000;
    }

    return 0;
}

/* Pth variant of usleep(3) */
int pth_usleep(unsigned int usec)
{
    pth_time_t until;
    pth_time_t offset;
    pth_event_t ev;

    /* short-circuit */
    if (usec == 0)
        return 0;

    /* calculate asleep time */
    offset = pth_time((long)(usec / 1000000), (long)(usec % 1000000));
    pth_time_set(&until, PTH_TIME_NOW);
    pth_time_add(&until, &offset);

    /* and let thread sleep until this time is elapsed */
    ev = pth_event(PTH_EVENT_TIME, until);
    if (ev == NULL)
        return pth_error(-1, errno);

    pth_wait(ev);

    pth_event_free(ev, PTH_FREE_THIS);

    return 0;
}

/* Pth variant of sleep(3) */
unsigned int pth_sleep(unsigned int sec)
{
    pth_time_t until;
    pth_time_t offset;
    pth_event_t ev;

    /* consistency check */
    if (sec == 0)
        return 0;

    /* calculate asleep time */
    offset = pth_time(sec, 0);
    pth_time_set(&until, PTH_TIME_NOW);
    pth_time_add(&until, &offset);

    /* and let thread sleep until this time is elapsed */
    ev = pth_event(PTH_EVENT_TIME, until);
    if (ev == NULL)
        return sec;

    pth_wait(ev);

    pth_event_free(ev, PTH_FREE_THIS);

    return 0;
}

/* Pth variant of POSIX pthread_sigmask(3) */
int pth_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
    int rv;

    /* change the explicitly remembered signal mask copy for the scheduler */
    if (set != NULL)
        pth_sc(sigprocmask)(how, &(pth_gctx_get()->pth_current->mctx.sigs), NULL);

    /* change the real (per-thread saved/restored) signal mask */
    rv = pth_sc(sigprocmask)(how, set, oset);

    return rv;
}

/* Pth variant of POSIX sigwait(3) */
int pth_sigwait(const sigset_t *set, int *sigp)
{
    return pth_sigwait_ev(set, sigp, NULL);
}

/* Pth variant of POSIX sigwait(3) with extra events */
int pth_sigwait_ev(const sigset_t *set, int *sigp, pth_event_t ev_extra)
{
    pth_event_t ev;
    sigset_t pending;
    int sig;

    if (set == NULL || sigp == NULL)
        return pth_error(EINVAL, EINVAL);

    /* check whether signal is already pending */
    if (sigpending(&pending) < 0)
        sigemptyset(&pending);
    for (sig = 1; sig < PTH_NSIG; sig++) {
        if (sigismember(set, sig) && sigismember(&pending, sig)) {
            pth_util_sigdelete(sig);
            *sigp = sig;
            return 0;
        }
    }

    /* create event and wait on it */
    if ((ev = pth_event(PTH_EVENT_SIGS|PTH_MODE_STATIC, &pth_gctx_get()->ev_key_sigwait_ev, set, sigp)) == NULL)
        return pth_error(errno, errno);
    if (ev_extra != NULL)
        pth_event_concat(ev, ev_extra, NULL);
    pth_wait(ev);
    if (ev_extra != NULL) {
        pth_event_isolate(ev);
        if (pth_event_status(ev) != PTH_STATUS_OCCURRED)
            return pth_error(EINTR, EINTR);
    }

    /* nothing to do, scheduler has already set *sigp for us */
    return 0;
}

/* Pth variant of waitpid(2) */
pid_t pth_waitpid(pid_t wpid, int *status, int options)
{
    pth_event_t ev;
    pid_t pid;

    pth_debug2("pth_waitpid: called from thread \"%s\"", pth_gctx_get()->pth_current->name);

    for (;;) {
        /* do a non-blocking poll for the pid */
        while (   (pid = pth_sc(waitpid)(wpid, status, options|WNOHANG)) < 0
               && errno == EINTR) ;

        /* if pid was found or caller requested a polling return immediately */
        if (pid == -1 || pid > 0 || (pid == 0 && (options & WNOHANG)))
            break;

        /* else wait a little bit */
        ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &pth_gctx_get()->ev_key_waitpid, pth_timeout(0,250000));
        pth_wait(ev);
    }

    pth_debug2("pth_waitpid: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return pid;
}

/* Pth variant of system(3) */
int pth_system(const char *cmd)
{
    struct sigaction sa_ign, sa_int, sa_quit;
    sigset_t ss_block, ss_old;
    struct stat sb;
    pid_t pid;
    int pstat;

    /* POSIX calling convention: determine whether the
       Bourne Shell ("sh") is available on this platform */
    if (cmd == NULL) {
        if (stat(PTH_PATH_BINSH, &sb) == -1)
            return 0;
        return 1;
    }

    /* temporarily ignore SIGINT and SIGQUIT actions */
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGINT,  &sa_ign, &sa_int);
    sigaction(SIGQUIT, &sa_ign, &sa_quit);

    /* block SIGCHLD signal */
    sigemptyset(&ss_block);
    sigaddset(&ss_block, SIGCHLD);
    pth_sc(sigprocmask)(SIG_BLOCK, &ss_block, &ss_old);

    /* fork the current process */
    pstat = -1;
    switch (pid = pth_fork()) {
        case -1: /* error */
            break;

        case 0:  /* child */
            /* restore original signal dispositions and execute the command */
            sigaction(SIGINT,  &sa_int,  NULL);
            sigaction(SIGQUIT, &sa_quit, NULL);
            pth_sc(sigprocmask)(SIG_SETMASK, &ss_old, NULL);

            /* stop the Pth scheduling */
            pth_scheduler_kill();

            /* execute the command through Bourne Shell */
            execl(PTH_PATH_BINSH, "sh", "-c", cmd, (char *)NULL);

            /* POSIX compliant return in case execution failed */
            exit(127);

        default: /* parent */
            /* wait until child process terminates */
            pid = pth_waitpid(pid, &pstat, 0);
            break;
    }

    /* restore original signal dispositions and execute the command */
    sigaction(SIGINT,  &sa_int,  NULL);
    sigaction(SIGQUIT, &sa_quit, NULL);
    pth_sc(sigprocmask)(SIG_SETMASK, &ss_old, NULL);

    /* return error or child process result code */
    return (pid == -1 ? -1 : pstat);
}

/* Pth variant of select(2) */
int pth_select(int nfds, fd_set *rfds, fd_set *wfds,
               fd_set *efds, struct timeval *timeout)
{
    return pth_select_ev(nfds, rfds, wfds, efds, timeout, NULL);
}

/* Pth variant of select(2) with extra events */
int pth_select_ev(int nfd, fd_set *rfds, fd_set *wfds,
                  fd_set *efds, struct timeval *timeout, pth_event_t ev_extra)
{
    pth_event_t ev_ring;
    pth_event_t ev_timeout;

    if(timeout && timeout->tv_sec == 0 && timeout->tv_usec == 0) {
        /* this should return immediately, so there is no need to manage blocking or timeouts */
        return pth_sc(select)(nfd, rfds, wfds, efds, timeout);
    }

    pth_implicit_init();
    pth_debug2("pth_select_ev: called from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX.1-2001/SUSv3 compliance */
    if (nfd < 0 || nfd > FD_SETSIZE)
        return pth_error(-1, EINVAL);
    if (timeout != NULL) {
        if (   timeout->tv_sec  < 0
            || timeout->tv_usec < 0
            || timeout->tv_usec >= 1000000 /* a full second */)
            return pth_error(-1, EINVAL);
        if (timeout->tv_sec > 31*24*60*60)
            timeout->tv_sec = 31*24*60*60;
    }

    /* first deal with the special situation of a plain microsecond delay */
    if (nfd == 0 && rfds == NULL && wfds == NULL && efds == NULL && timeout != NULL) {
        /* block for the timeout */
        pth_event_t ev = pth_event(PTH_EVENT_TIME, pth_timeout(timeout->tv_sec, timeout->tv_usec));

        if (ev_extra != NULL) {
            pth_event_concat(ev, ev_extra, NULL);
        }

        /* go to the scheduler to wait for the timeout */
        pth_wait(ev);

        /* back from the scheduler */
		pth_status_t ev_status = pth_event_status(ev);
		pth_event_isolate(ev);
		pth_event_free(ev, PTH_FREE_THIS);

        if (ev_extra != NULL && ev_status != PTH_STATUS_OCCURRED) {
			return pth_error(-1, EINTR);
        }

        /* POSIX.1-2001/SUSv3 compliance */
        if (rfds != NULL) FD_ZERO(rfds);
        if (wfds != NULL) FD_ZERO(wfds);
        if (efds != NULL) FD_ZERO(efds);
        return 0;
    }

    int fd;
    ev_ring = NULL;
    for (fd = 0; fd < nfd; fd++) {
    	uint32_t events = 0;
    	unsigned long goal = PTH_EVENT_FD;

        if (rfds != NULL) {
            if (FD_ISSET(fd, rfds)) {
                events |= EPOLLIN;
                goal |= PTH_UNTIL_FD_READABLE;
            }
        }
        if (wfds != NULL) {
            if (FD_ISSET(fd, wfds)) {
                events |= EPOLLOUT;
                goal |= PTH_UNTIL_FD_WRITEABLE;
            }
        }
        if (efds != NULL) {
            if (FD_ISSET(fd, efds)) {
                events |= EPOLLERR;
                goal |= PTH_UNTIL_FD_EXCEPTION;
            }
        }

        if(events != 0) {
        	pth_event_t ev = pth_event(goal, fd);
        	if(ev_ring == NULL) {
        		ev_ring = ev;
        	} else {
        		pth_event_concat(ev_ring, ev, NULL);
        	}
        }
    }

    ev_timeout = NULL;
    if (timeout != NULL) {
        ev_timeout = pth_event(PTH_EVENT_TIME, pth_timeout(timeout->tv_sec, timeout->tv_usec));
    	if(ev_ring == NULL) {
    		ev_ring = ev_timeout;
    	} else {
    		pth_event_concat(ev_ring, ev_timeout, NULL);
    	}
    }

    if (ev_extra != NULL) {
    	if(ev_ring == NULL) {
    		ev_ring = ev_extra;
    	} else {
    		pth_event_concat(ev_ring, ev_extra, NULL);
    	}
    }

    /* suspend current thread until one filedescriptor
       is ready or the timeout occurred */
    pth_wait(ev_ring);

    /* remove extra event from ring */
    if (ev_extra != NULL) pth_event_isolate(ev_extra);

    /* remove and handle timeout, cleanup timeout */
    int timeout_occurred = FALSE;
    if (ev_timeout != NULL) {
    	pth_event_isolate(ev_timeout);
    	timeout_occurred = (pth_event_status(ev_timeout) == PTH_STATUS_OCCURRED) ? TRUE : FALSE;
		pth_event_free(ev_timeout, PTH_FREE_THIS);
		ev_timeout = NULL;
    }

    int select_failed = FALSE;
    int select_occurred = FALSE;

    /* the remaining events in the ring are for the select call */
    pth_event_t ev_iter = ev_ring;
    while(ev_iter != NULL) {
    	pth_status_t ev_status = pth_event_status(ev_iter);

    	if (ev_status == PTH_STATUS_FAILED) {
    		select_failed = TRUE;
    	}
    	if(ev_status == PTH_STATUS_OCCURRED) {
    		select_occurred = TRUE;
    	}

    	ev_iter = pth_event_walk(ev_iter, PTH_WALK_NEXT);
    	if(ev_iter == ev_ring) ev_iter = NULL;
    }

    /* select return code semantics */
    if (select_failed) {
    	pth_event_free(ev_ring, PTH_FREE_ALL);
    	return pth_error(-1, EBADF);
    }

	/* POSIX.1-2001/SUSv3 compliance */
	if (rfds != NULL) FD_ZERO(rfds);
	if (wfds != NULL) FD_ZERO(wfds);
	if (efds != NULL) FD_ZERO(efds);

    if (timeout_occurred) {
    	/* return empty fd sets */
    	pth_event_free(ev_ring, PTH_FREE_ALL);
    	return 0;
    }

    if(select_occurred) {
        /* mark the correct fds, and count */
    	int num_fds_ready = 0;
        ev_iter = ev_ring;
    	while(ev_iter != NULL) {
    		if(rfds != NULL && (ev_iter->ev_goal & PTH_UNTIL_FD_READABLE)) {
    			FD_SET(ev_iter->ev_args.FD.fd, rfds);
    			num_fds_ready++;
    		}
    		if(wfds != NULL && (ev_iter->ev_goal & PTH_UNTIL_FD_WRITEABLE)) {
    			FD_SET(ev_iter->ev_args.FD.fd, wfds);
    			num_fds_ready++;
    		}
    		if(efds != NULL && (ev_iter->ev_goal & PTH_UNTIL_FD_EXCEPTION)) {
    			FD_SET(ev_iter->ev_args.FD.fd, efds);
    			num_fds_ready++;
    		}
    		ev_iter = pth_event_walk(ev_iter, PTH_WALK_NEXT);
    		if(ev_iter == ev_ring) ev_iter = NULL;
    	}

    	pth_event_free(ev_ring, PTH_FREE_ALL);
    	return num_fds_ready;
    } else if(ev_extra != NULL) {
    	/* select did not occur */
        return pth_error(-1, EINTR);
    } else {
    	return 0;
    }
}

/* Pth variant of pth_pselect(2) */
int pth_pselect(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds,
                const struct timespec *ts, const sigset_t *mask)
{
    sigset_t omask;
    struct timeval tv;
    struct timeval *tvp;
    int rv;

    /* convert timeout */
    if (ts != NULL) {
        tv.tv_sec  = ts->tv_sec;
        tv.tv_usec = ts->tv_nsec / 1000;
        tvp = &tv;
    }
    else
        tvp = NULL;

    /* optionally set signal mask */
    if (mask != NULL)
        if (pth_sc(sigprocmask)(SIG_SETMASK, mask, &omask) < 0)
            return pth_error(-1, errno);

    rv = pth_select(nfds, rfds, wfds, efds, tvp);

    /* optionally set signal mask */
    if (mask != NULL)
        pth_shield { pth_sc(sigprocmask)(SIG_SETMASK, &omask, NULL); }

    return rv;
}

/* Pth variant of poll(2) */
int pth_poll(struct pollfd *pfd, nfds_t nfd, int timeout)
{
    return pth_poll_ev(pfd, nfd, timeout, NULL);
}

/* Pth variant of poll(2) with extra events */
int pth_poll_ev(struct pollfd *pfd, nfds_t nfd, int timeout, pth_event_t ev_extra)
{
    pth_event_t ev_epoll;
    pth_event_t ev_timeout;
    int i, rc;

    if(timeout == 0) {
        /* this should return immediately, so there is no need to manage blocking or timeouts */
        return pth_sc(poll)(pfd, nfd, 0);
    }

    pth_implicit_init();
    pth_debug2("pth_poll_ev: called from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* argument sanity checks */
    if (pfd == NULL)
        return pth_error(-1, EFAULT);
    if (nfd > FD_SETSIZE)
        return pth_error(-1, EINVAL);

    int epfd_tmp = pth_sc(epoll_create)(1);
    if(epfd_tmp < 0)
        return pth_error(-1, EINVAL);

    /* if any are files, then we are instantly ready
     * and epoll doesnt support files */
    int need_wait = 1;
    int epoll_failed = 0;
    int epoll_ready = 1;

    for (i = 0; i < nfd; i++) {
        struct epoll_event epev;
        memset(&epev, 0, sizeof(struct epoll_event));

        epev.data.fd = pfd[i].fd;
        if (pfd[i].events & (POLLIN|POLLRDNORM))
            epev.events |= EPOLLIN;
        if (pfd[i].events & (POLLOUT|POLLWRNORM|POLLWRBAND))
            epev.events |= EPOLLOUT;
        if (pfd[i].events & (POLLPRI|POLLRDBAND))
            epev.events |= EPOLLERR;

        rc = pth_sc(epoll_ctl)(epfd_tmp, EPOLL_CTL_ADD, pfd[i].fd, &epev);
        if(rc < 0 && errno == EPERM) {
            /* there is a file in the set, it will be ready so we can poll immediately */
            need_wait = 0;
            pth_sc(close)(epfd_tmp);
            break;
        } else if(rc < 0) {
            pth_sc(close)(epfd_tmp);
            return pth_error(-1, errno);
        }
    }

    if(need_wait) {
        /* suspend current thread until one filedescriptor in events is ready,
         * (in which case our outer epfd_tmp will be readable)
           or the timeout occurred */
        ev_epoll = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, epfd_tmp);

        ev_timeout = NULL;
        if (timeout != 0) {
            if(timeout == -1)
                timeout = 1000*60*60*24;
            /* timeout is in milliseconds */
            long sec = (long)(timeout / 1000);
            long usec = (long)((timeout % 1000) * 1000);
            ev_timeout = pth_event(PTH_EVENT_TIME, pth_timeout(sec, usec));
            pth_event_concat(ev_epoll, ev_timeout, NULL);
        }
        if (ev_extra != NULL)
            pth_event_concat(ev_epoll, ev_extra, NULL);

        pth_wait(ev_epoll);

        /* we are ready, stop waiting for timeout */
        if (ev_extra != NULL)
            pth_event_isolate(ev_extra);
        if (timeout != 0)
            pth_event_isolate(ev_timeout);

        for (i = 0; i < nfd; i++) {
            rc = pth_sc(epoll_ctl)(epfd_tmp, EPOLL_CTL_DEL, pfd[i].fd, NULL);
        }
        rc = pth_sc(close)(epfd_tmp);

        /* return code semantics */
        epoll_failed = pth_event_status(ev_epoll) == PTH_STATUS_FAILED;
        epoll_ready = pth_event_status(ev_epoll) == PTH_STATUS_OCCURRED ||
                (timeout != 0 && pth_event_status(ev_timeout) == PTH_STATUS_OCCURRED);

        if(ev_timeout != NULL) {
            pth_event_free(ev_timeout, PTH_FREE_THIS);
        }
        if(ev_epoll != NULL) {
            pth_event_free(ev_epoll, PTH_FREE_THIS);
        }
    }

    pth_debug2("pth_epoll_wait_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);

    if (epoll_failed) {
        return pth_error(-1, EBADF);
    } else if(epoll_ready) {
        return pth_sc(poll)(pfd, nfd, 0);
    } else if(ev_extra != NULL) {
        return pth_error(-1, EINTR);
    } else {
        return 0;
    }
}

int pth_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *ts, const sigset_t *mask)
{
    sigset_t omask;
    int timeout;
    int rv;

    /* convert timeout */
    timeout = (ts == NULL) ? (-1) : (((ts->tv_sec * 1000) + (ts->tv_nsec / 1000000)));

    /* optionally set signal mask */
    if (mask != NULL)
        if (pth_sc(sigprocmask)(SIG_SETMASK, mask, &omask) < 0)
            return pth_error(-1, errno);

    rv = pth_poll(fds, nfds, timeout);

    /* optionally set signal mask */
    if (mask != NULL)
        pth_shield { pth_sc(sigprocmask)(SIG_SETMASK, &omask, NULL); }

    return rv;
}

int pth_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    return pth_epoll_wait_ev(epfd, events, maxevents, timeout, NULL);
}

int pth_epoll_wait_ev(int epfd, struct epoll_event *events, int maxevents, int timeout, pth_event_t ev_extra) {
    pth_event_t ev_epoll;
    pth_event_t ev_timeout;

    if(timeout == 0) {
        /* this should return immediately, so there is no need to manage blocking or timeouts */
        return pth_sc(epoll_wait)(epfd, events, maxevents, 0);
    }

    pth_implicit_init();
    pth_debug2("pth_epoll_wait_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if(maxevents <= 0)
        return pth_error(-1, EINVAL);
    if (!pth_util_fd_valid(epfd))
        return pth_error(-1, EBADF);

    /* suspend current thread until one filedescriptor in events is ready,
     * (in which case our outer epfd_tmp will be readable)
       or the timeout occurred */
    ev_epoll = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, epfd);

    ev_timeout = NULL;
    if (timeout != 0) {
        if(timeout == -1)
            timeout = 1000*60*60*24;
        /* timeout is in milliseconds */
        long sec = (long)(timeout / 1000);
        long usec = (long)((timeout % 1000) * 1000);
        ev_timeout = pth_event(PTH_EVENT_TIME, pth_timeout(sec, usec));
        pth_event_concat(ev_epoll, ev_timeout, NULL);
    }
    if (ev_extra != NULL)
        pth_event_concat(ev_epoll, ev_extra, NULL);

    pth_wait(ev_epoll);

    /* we are ready, stop waiting for timeout */
    if (ev_extra != NULL)
        pth_event_isolate(ev_extra);
    if (timeout != 0)
        pth_event_isolate(ev_timeout);

    /* return code semantics */
    int epoll_failed = pth_event_status(ev_epoll) == PTH_STATUS_FAILED;
    int epoll_ready = pth_event_status(ev_epoll) == PTH_STATUS_OCCURRED ||
            (timeout != 0 && pth_event_status(ev_timeout) == PTH_STATUS_OCCURRED);

    if(ev_timeout != NULL) {
    	pth_event_free(ev_timeout, PTH_FREE_THIS);
    }
    if(ev_epoll != NULL) {
    	pth_event_free(ev_epoll, PTH_FREE_THIS);
    }

    pth_debug2("pth_epoll_wait_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);

    if (epoll_failed) {
        return pth_error(-1, EBADF);
    } else if(epoll_ready) {
        return pth_sc(epoll_wait)(epfd, events, maxevents, 0);
    } else if(ev_extra != NULL) {
        return pth_error(-1, EINTR);
    } else {
    	return 0;
    }
}

int pth_epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *mask) {
    sigset_t omask;
    int rv;

    /* optionally set signal mask */
    if (mask != NULL)
        if (pth_sc(sigprocmask)(SIG_SETMASK, mask, &omask) < 0)
            return pth_error(-1, errno);

    rv = pth_epoll_wait(epfd, events, maxevents, timeout);

    /* optionally set signal mask */
    if (mask != NULL)
        pth_shield { pth_sc(sigprocmask)(SIG_SETMASK, &omask, NULL); }

    return rv;
}

/* Pth variant of connect(2) */
int pth_connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    return pth_connect_ev(s, addr, addrlen, NULL);
}

/* Pth variant of connect(2) with extra events */
int pth_connect_ev(int s, const struct sockaddr *addr, socklen_t addrlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    int rv, err;
    socklen_t errlen;
    int fdmode;

    pth_implicit_init();
    pth_debug2("pth_connect_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (!pth_util_fd_valid(s))
        return pth_error(-1, EBADF);

    /* force filedescriptor into non-blocking mode */
    if ((fdmode = pth_fdmode(s, PTH_FDMODE_NONBLOCK)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* try to connect */
    while (   (rv = pth_sc(connect)(s, (struct sockaddr *)addr, addrlen)) == -1
           && errno == EINTR)
        ;

    /* restore filedescriptor mode */
    pth_shield { pth_fdmode(s, fdmode); }

    /* if it is still on progress wait until socket is really writeable */
    if (rv == -1 && errno == EINPROGRESS && fdmode != PTH_FDMODE_NONBLOCK) {
    	ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE, s);
        if (ev == NULL)
            return pth_error(-1, errno);

        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);

        pth_wait(ev);

        if (ev_extra != NULL)
            pth_event_isolate(ev);

        int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
        pth_event_free(ev, PTH_FREE_THIS);

        if (ev_extra != NULL && !ev_occurred) {
			return pth_error(-1, EINTR);
        }

        errlen = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen) == -1)
            return -1;
        if (err == 0)
            return 0;
        return pth_error(rv, err);
    }

    pth_debug2("pth_connect_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return rv;
}

/* Pth variant of accept(2) */
int pth_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    return pth_accept_ev(s, addr, addrlen, NULL);
}

/* Pth variant of accept(2) with extra events */
int pth_accept_ev(int s, struct sockaddr *addr, socklen_t *addrlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    int rv;

    pth_implicit_init();
    pth_debug2("pth_accept_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (!pth_util_fd_valid(s))
        return pth_error(-1, EBADF);

    /* force filedescriptor into non-blocking mode */
    if ((fdmode = pth_fdmode(s, PTH_FDMODE_NONBLOCK)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* NOTE from now on we need to return via the done goto to make sure the fd returns to
     * its old blocking/nonblocking mode */

    /* poll socket via accept */
    ev = NULL;
    while ((rv = pth_sc(accept)(s, addr, addrlen)) == -1
           && (errno == EAGAIN || errno == EWOULDBLOCK)
           && fdmode != PTH_FDMODE_NONBLOCK) {
        /* do lazy event allocation */
		ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, s);
		if (ev == NULL) {
			rv = pth_error(-1, errno);
		    goto done;
		}

		if (ev_extra != NULL)
			pth_event_concat(ev, ev_extra, NULL);

        /* wait until accept has a chance */
        pth_wait(ev);

        if (ev_extra != NULL)
            pth_event_isolate(ev);

        int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
        pth_event_free(ev, PTH_FREE_THIS);

        /* check for the extra events */
        if (ev_extra != NULL && !ev_occurred) {
			pth_fdmode(s, fdmode);
			rv = pth_error(-1, EINTR);
			goto done;
        }
    }

done:
    /* restore filedescriptor mode */
    pth_shield {
        pth_fdmode(s, fdmode);
        if (rv != -1)
            pth_fdmode(rv, fdmode);
    }

    pth_debug2("pth_accept_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return rv;
}

/* Pth variant of read(2) */
ssize_t pth_read(int fd, void *buf, size_t nbytes)
{
    return pth_read_ev(fd, buf, nbytes, NULL);
}

/* Pth variant of read(2) with extra event(s) */
ssize_t pth_read_ev(int fd, void *buf, size_t nbytes, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    int n;

    pth_implicit_init();
    pth_debug2("pth_read_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);

    /* check mode of filedescriptor */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_POLL)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode == PTH_FDMODE_BLOCK) {
        /* let thread sleep until fd is readable or the extra event occurs */
        ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, fd);
		if (ev == NULL)
			return pth_error(-1, errno);

        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);

        n = pth_wait(ev);

        if (ev_extra != NULL)
            pth_event_isolate(ev);

        int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
        pth_event_free(ev, PTH_FREE_THIS);

        /* check for the extra events */
        if (ev_extra != NULL && !ev_occurred) {
			return pth_error(-1, EINTR);
        }
    }

    /* Now perform the actual read. We're now guaranteed to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next read(2) call will not block.  But keep
       in mind, that only 1 next read(2) call is guaranteed to not block
       (except for the EINTR situation). */
    while ((n = pth_sc(read)(fd, buf, nbytes)) < 0
           && errno == EINTR) ;

    pth_debug2("pth_read_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return n;
}

/* Pth variant of write(2) */
ssize_t pth_write(int fd, const void *buf, size_t nbytes)
{
    return pth_write_ev(fd, buf, nbytes, NULL);
}

/* Pth variant of write(2) with extra event(s) */
ssize_t pth_write_ev(int fd, const void *buf, size_t nbytes, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    ssize_t rv;
    ssize_t s;

    pth_implicit_init();
    pth_debug2("pth_write_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);

    /* force filedescriptor into non-blocking mode */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_NONBLOCK)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* NOTE from now on we need to return via the done goto to make sure the fd returns to
     * its old blocking/nonblocking mode */

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode != PTH_FDMODE_NONBLOCK) {
        rv = 0;
        for (;;) {
            /* let thread sleep until fd is writable or event occurs */
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE, fd);
            if (ev == NULL) {
                rv = pth_error(-1, errno);
                goto done;
            }

            if (ev_extra != NULL)
                pth_event_concat(ev, ev_extra, NULL);

            pth_wait(ev);

            if (ev_extra != NULL)
                pth_event_isolate(ev);

            int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
            pth_event_free(ev, PTH_FREE_THIS);

            /* check for the extra events */
            if (ev_extra != NULL && !ev_occurred) {
                pth_fdmode(s, fdmode);
                rv = pth_error(-1, EINTR);
                goto done;
            }

            /* now perform the actual write operation */
            while ((s = pth_sc(write)(fd, buf, nbytes)) < 0
                   && errno == EINTR) ;
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of write(2). */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                buf = (void *)((char *)buf + s);
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    }
    else {
        /* just perform the actual write operation */
        while ((rv = pth_sc(write)(fd, buf, nbytes)) < 0
               && errno == EINTR) ;
    }

done:
    /* restore filedescriptor mode */
    pth_shield { pth_fdmode(fd, fdmode); }

    pth_debug2("pth_write_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return rv;
}

/* Pth variant of readv(2) */
ssize_t pth_readv(int fd, const struct iovec *iov, int iovcnt)
{
    return pth_readv_ev(fd, iov, iovcnt, NULL);
}

/* Pth variant of readv(2) with extra event(s) */
ssize_t pth_readv_ev(int fd, const struct iovec *iov, int iovcnt, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    int n;

    pth_implicit_init();
    pth_debug2("pth_readv_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV)
        return pth_error(-1, EINVAL);
    if (iovcnt == 0)
        return 0;

    /* check mode of filedescriptor */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_POLL)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode == PTH_FDMODE_BLOCK) {
        /* let thread sleep until fd is readable or event occurs */
        ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, fd);
        if (ev == NULL)
			return pth_error(-1, errno);

        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);

        n = pth_wait(ev);

        if (ev_extra != NULL)
            pth_event_isolate(ev);

        int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
        pth_event_free(ev, PTH_FREE_THIS);

        /* check for the extra events */
        if (ev_extra != NULL && !ev_occurred) {
			return pth_error(-1, EINTR);
        }
    }

    /* Now perform the actual read. We're now guaranteed to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next read(2) call will not block.  But keep
       in mind, that only 1 next read(2) call is guaranteed to not block
       (except for the EINTR situation). */
#if PTH_FAKE_RWV
    while ((n = pth_readv_faked(fd, iov, iovcnt)) < 0
           && errno == EINTR) ;
#else
    while ((n = pth_sc(readv)(fd, iov, iovcnt)) < 0
           && errno == EINTR) ;
#endif

    pth_debug2("pth_readv_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return n;
}

/* A faked version of readv(2) */
intern ssize_t pth_readv_faked(int fd, const struct iovec *iov, int iovcnt)
{
    char *buffer;
    size_t bytes, copy, rv;
    int i;

    /* determine total number of bytes to read */
    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            return pth_error((ssize_t)(-1), EINVAL);
        bytes += iov[i].iov_len;
    }
    if (bytes <= 0)
        return pth_error((ssize_t)(-1), EINVAL);

    /* allocate a temporary buffer */
    if ((buffer = (char *)malloc(bytes)) == NULL)
        return (ssize_t)(-1);

    /* read data into temporary buffer (caller guaranteed us to not block) */
    rv = pth_sc(read)(fd, buffer, bytes);

    /* scatter read data into callers vector */
    if (rv > 0) {
        bytes = rv;
        for (i = 0; i < iovcnt; i++) {
            copy = pth_util_min(iov[i].iov_len, bytes);
            memcpy(iov[i].iov_base, buffer, copy);
            buffer += copy;
            bytes  -= copy;
            if (bytes <= 0)
                break;
        }
    }

    /* remove the temporary buffer */
    pth_shield { free(buffer); }

    /* return number of read bytes */
    return(rv);
}

/* Pth variant of writev(2) */
ssize_t pth_writev(int fd, const struct iovec *iov, int iovcnt)
{
    return pth_writev_ev(fd, iov, iovcnt, NULL);
}

/* Pth variant of writev(2) with extra event(s) */
ssize_t pth_writev_ev(int fd, const struct iovec *iov, int iovcnt, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    struct iovec *liov;
    int liovcnt;
    size_t nbytes;
    ssize_t rv;
    ssize_t s;
    struct iovec tiov_stack[32];
    struct iovec *tiov;
    int tiovcnt;

    pth_implicit_init();
    pth_debug2("pth_writev_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV)
        return pth_error(-1, EINVAL);
    if (iovcnt == 0)
        return 0;

    /* force filedescriptor into non-blocking mode */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_NONBLOCK)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* NOTE from now on we need to return via the done goto to make sure the fd returns to
     * its old blocking/nonblocking mode */

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode != PTH_FDMODE_NONBLOCK) {

        /* init return value and number of bytes to write */
        rv      = 0;
        nbytes  = pth_writev_iov_bytes(iov, iovcnt);

        if(nbytes == 0) {
            rv = 0;
            goto done;
        }

        /* provide temporary iovec structure */
        if (iovcnt > sizeof(tiov_stack)) {
            tiovcnt = (sizeof(struct iovec) * UIO_MAXIOV);
            if ((tiov = (struct iovec *)malloc(tiovcnt)) == NULL) {
                rv = pth_error(-1, errno);
                goto done;
            }
        }
        else {
            tiovcnt = sizeof(tiov_stack);
            tiov    = tiov_stack;
        }

        /* init local iovec structure */
        liov    = NULL;
        liovcnt = 0;
        pth_writev_iov_advance(iov, iovcnt, 0, &liov, &liovcnt, tiov, tiovcnt);

        for (;;) {
            /* let thread sleep until fd is writeable or event occurs */
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE, fd);
            if (ev == NULL) {
    			rv = pth_error(-1, errno);
    			goto done;
            }

            if (ev_extra != NULL)
                pth_event_concat(ev, ev_extra, NULL);

            pth_wait(ev);

            if (ev_extra != NULL)
                pth_event_isolate(ev);

            int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
            pth_event_free(ev, PTH_FREE_THIS);

            /* check for the extra events */
            if (ev_extra != NULL && !ev_occurred) {
				pth_fdmode(fd, fdmode);
				if (iovcnt > sizeof(tiov_stack))
					free(tiov);
    			rv = pth_error(-1, EINTR);
    			goto done;
            }

            /* now perform the actual write operation */
#if PTH_FAKE_RWV
            while ((s = pth_writev_faked(fd, liov, liovcnt)) < 0
                   && errno == EINTR) ;
#else
            while ((s = pth_sc(writev)(fd, liov, liovcnt)) < 0
                   && errno == EINTR) ;
#endif
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of writev(2) */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                pth_writev_iov_advance(iov, iovcnt, s, &liov, &liovcnt, tiov, tiovcnt);
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }

        /* cleanup */
        if (iovcnt > sizeof(tiov_stack))
            free(tiov);
    }
    else {
        /* just perform the actual write operation */
#if PTH_FAKE_RWV
        while ((rv = pth_writev_faked(fd, iov, iovcnt)) < 0
               && errno == EINTR) ;
#else
        while ((rv = pth_sc(writev)(fd, iov, iovcnt)) < 0
               && errno == EINTR) ;
#endif
    }

done:
    /* restore filedescriptor mode */
    pth_shield { pth_fdmode(fd, fdmode); }

    pth_debug2("pth_writev_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return rv;
}

/* calculate number of bytes in a struct iovec */
intern ssize_t pth_writev_iov_bytes(const struct iovec *iov, int iovcnt)
{
    ssize_t bytes;
    int i;

    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            continue;
        bytes += iov[i].iov_len;
    }
    return bytes;
}

/* advance the virtual pointer of a struct iov */
intern void pth_writev_iov_advance(const struct iovec *riov, int riovcnt, size_t advance,
                                   struct iovec **liov, int *liovcnt,
                                   struct iovec *tiov, int tiovcnt)
{
    int i;

    if (*liov == NULL && *liovcnt == 0) {
        /* initialize with real (const) structure on first step */
        *liov = (struct iovec *)riov;
        *liovcnt = riovcnt;
    }
    if (advance > 0) {
        if (*liov == riov && *liovcnt == riovcnt) {
            /* reinitialize with a copy to be able to adjust it */
            *liov = &tiov[0];
            for (i = 0; i < riovcnt; i++) {
                tiov[i].iov_base = riov[i].iov_base;
                tiov[i].iov_len  = riov[i].iov_len;
            }
        }
        /* advance the virtual pointer */
        while (*liovcnt > 0 && advance > 0) {
            if ((*liov)->iov_len > advance) {
                (*liov)->iov_base = (char *)((*liov)->iov_base) + advance;
                (*liov)->iov_len -= advance;
                break;
            }
            else {
                advance -= (*liov)->iov_len;
                (*liovcnt)--;
                (*liov)++;
            }
        }
    }
    return;
}

/* A faked version of writev(2) */
intern ssize_t pth_writev_faked(int fd, const struct iovec *iov, int iovcnt)
{
    char *buffer, *cp;
    size_t bytes, to_copy, copy, rv;
    int i;

    /* determine total number of bytes to write */
    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            return pth_error((ssize_t)(-1), EINVAL);
        bytes += iov[i].iov_len;
    }
    if (bytes <= 0)
        return pth_error((ssize_t)(-1), EINVAL);

    /* allocate a temporary buffer to hold the data */
    if ((buffer = (char *)malloc(bytes)) == NULL)
        return (ssize_t)(-1);

    /* concatenate the data from callers vector into buffer */
    to_copy = bytes;
    cp = buffer;
    for (i = 0; i < iovcnt; i++) {
         copy = pth_util_min(iov[i].iov_len, to_copy);
         memcpy(cp, iov[i].iov_base, copy);
         to_copy -= copy;
         if (to_copy <= 0)
             break;
    }

    /* write continuous chunck of data (caller guaranteed us to not block) */
    rv = pth_sc(write)(fd, buffer, bytes);

    /* remove the temporary buffer */
    pth_shield { free(buffer); }

    return(rv);
}

/* Pth variant of POSIX pread(3) */
ssize_t pth_pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    off_t old_offset;
    ssize_t rc;

    /* protect us: pth_read can yield! */
    if (!pth_mutex_acquire(&pth_gctx_get()->mutex_pread, FALSE, NULL))
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek(fd, 0, SEEK_CUR)) == (off_t)(-1)) {
        pth_mutex_release(&pth_gctx_get()->mutex_pread);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek(fd, offset, SEEK_SET) == (off_t)(-1)) {
        pth_mutex_release(&pth_gctx_get()->mutex_pread);
        return (-1);
    }

    /* perform the read operation */
    rc = pth_read(fd, buf, nbytes);

    /* restore the old offset situation */
    pth_shield { lseek(fd, old_offset, SEEK_SET); }

    /* unprotect and return result of read */
    pth_mutex_release(&pth_gctx_get()->mutex_pread);
    return rc;
}

/* Pth variant of POSIX pwrite(3) */
ssize_t pth_pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    off_t old_offset;
    ssize_t rc;

    /* protect us: pth_write can yield! */
    if (!pth_mutex_acquire(&pth_gctx_get()->mutex_pwrite, FALSE, NULL))
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek(fd, 0, SEEK_CUR)) == (off_t)(-1)) {
        pth_mutex_release(&pth_gctx_get()->mutex_pwrite);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek(fd, offset, SEEK_SET) == (off_t)(-1)) {
        pth_mutex_release(&pth_gctx_get()->mutex_pwrite);
        return (-1);
    }

    /* perform the write operation */
    rc = pth_write(fd, buf, nbytes);

    /* restore the old offset situation */
    pth_shield { lseek(fd, old_offset, SEEK_SET); }

    /* unprotect and return result of write */
    pth_mutex_release(&pth_gctx_get()->mutex_pwrite);
    return rc;
}

/* Pth variant of SUSv2 recv(2) */
ssize_t pth_recv(int s, void *buf, size_t len, int flags)
{
    return pth_recv_ev(s, buf, len, flags, NULL);
}

/* Pth variant of SUSv2 recv(2) with extra event(s) */
ssize_t pth_recv_ev(int s, void *buf, size_t len, int flags, pth_event_t ev)
{
    return pth_recvfrom_ev(s, buf, len, flags, NULL, 0, ev);
}

/* Pth variant of SUSv2 recvfrom(2) */
ssize_t pth_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    return pth_recvfrom_ev(s, buf, len, flags, from, fromlen, NULL);
}

/* Pth variant of SUSv2 recvfrom(2) with extra event(s) */
ssize_t pth_recvfrom_ev(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    int n;

    pth_implicit_init();
    pth_debug2("pth_recvfrom_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);

    /* check mode of filedescriptor */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_POLL)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode == PTH_FDMODE_BLOCK) {
        if (!pth_util_fd_valid(fd))
            return pth_error(-1, EBADF);

        /* let thread sleep until fd is readable or the extra event occurs */
        ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE, fd);
		if (ev == NULL)
			return pth_error(-1, errno);

		if (ev_extra != NULL)
			pth_event_concat(ev, ev_extra, NULL);

		pth_wait(ev);

		if (ev_extra != NULL)
			pth_event_isolate(ev);

		int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
		pth_event_free(ev, PTH_FREE_THIS);

		/* check for the extra events */
		if (ev_extra != NULL && !ev_occurred) {
			return pth_error(-1, EINTR);
		}
    }

    /* now perform the actual read. We're now guaranteed to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next recvfrom(2) call will not block.  But keep
       in mind, that only 1 next recvfrom(2) call is guaranteed to not block
       (except for the EINTR situation). */
    while ((n = pth_sc(recvfrom)(fd, buf, nbytes, flags, from, fromlen)) < 0
           && errno == EINTR) ;

    pth_debug2("pth_recvfrom_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return n;
}

/* Pth variant of SUSv2 send(2) */
ssize_t pth_send(int s, const void *buf, size_t len, int flags)
{
    return pth_send_ev(s, buf, len, flags, NULL);
}

/* Pth variant of SUSv2 send(2) with extra event(s) */
ssize_t pth_send_ev(int s, const void *buf, size_t len, int flags, pth_event_t ev)
{
    return pth_sendto_ev(s, buf, len, flags, NULL, 0, ev);
}

/* Pth variant of SUSv2 sendto(2) */
ssize_t pth_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    return pth_sendto_ev(s, buf, len, flags, to, tolen, NULL);
}

/* Pth variant of SUSv2 sendto(2) with extra event(s) */
ssize_t pth_sendto_ev(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen, pth_event_t ev_extra)
{
    pth_event_t ev;
    int fdmode;
    ssize_t rv;
    ssize_t s;

    pth_implicit_init();
    pth_debug2("pth_sendto_ev: enter from thread \"%s\"", pth_gctx_get()->pth_current->name);

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;
    if (!pth_util_fd_valid(fd))
        return pth_error(-1, EBADF);

    /* force filedescriptor into non-blocking mode */
    if ((fdmode = pth_fdmode(fd, PTH_FDMODE_NONBLOCK)) == PTH_FDMODE_ERROR)
        return pth_error(-1, EBADF);

    /* NOTE from now on we need to return via the done goto to make sure the fd returns to
     * its old blocking/nonblocking mode */

    /* poll filedescriptor if not already in non-blocking operation */
    if (fdmode != PTH_FDMODE_NONBLOCK) {
        if (!pth_util_fd_valid(fd)) {
            pth_fdmode(fd, fdmode);
            rv = pth_error(-1, EBADF);
            goto done;
        }

        rv = 0;
        for (;;) {
            /* let thread sleep until fd is writeable or event occurs */
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE, fd);
    		if (ev == NULL) {
    			rv = pth_error(-1, errno);
    		    goto done;
    		}

    		if (ev_extra != NULL)
    			pth_event_concat(ev, ev_extra, NULL);

    		pth_wait(ev);

    		if (ev_extra != NULL)
    			pth_event_isolate(ev);

    		int ev_occurred = pth_event_status(ev) == PTH_STATUS_OCCURRED;
    		pth_event_free(ev, PTH_FREE_THIS);

    		/* check for the extra events */
    		if (ev_extra != NULL && !ev_occurred) {
				pth_fdmode(fd, fdmode);
    			rv = pth_error(-1, EINTR);
    			goto done;
    		}

            /* now perform the actual send operation */
            while ((s = pth_sc(sendto)(fd, buf, nbytes, flags, to, tolen)) < 0
                   && errno == EINTR) ;
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of write(2). */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                buf = (void *)((char *)buf + s);
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    }
    else {
        /* just perform the actual send operation */
        while ((rv = pth_sc(sendto)(fd, buf, nbytes, flags, to, tolen)) < 0
               && errno == EINTR) ;
    }

done:
    /* restore filedescriptor mode */
    pth_shield { pth_fdmode(fd, fdmode); }

    pth_debug2("pth_sendto_ev: leave to thread \"%s\"", pth_gctx_get()->pth_current->name);
    return rv;
}

