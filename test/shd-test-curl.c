/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>

/* curl stuff */
#include <curl/curl.h>

/*
 * Simply download a HTTP file.
 *
 * ! I think this creates threads automatically. might need to reconfigure to avoid this.
 *
 * gcc -lcurl -g shd-test-curl.c -o curlc
 *
 */
int main(void) {
    CURL *http_handle;
    CURLM *multi_handle;

    int still_running; /* keep number of running handles */

    http_handle = curl_easy_init();

    /* set the options (I left out a few, you'll get the point anyway) */
    curl_easy_setopt(http_handle, CURLOPT_URL, "http://www-users.cs.umn.edu/~jansen/temp/topology_dec16.xml.xz");

    /* init a multi stack */
    multi_handle = curl_multi_init();

    /* add the individual transfers */
    curl_multi_add_handle(multi_handle, http_handle);

    /* we start some action by calling perform right away */
    curl_multi_perform(multi_handle, &still_running);

    while (still_running) {
        struct timeval timeout;
        int rc; /* select() return code */

        fd_set fdread;
        fd_set fdwrite;
        fd_set fdexcep;
        int maxfd = -1;

        long curl_timeo = -1;

        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        /* set a suitable timeout to play around with */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        curl_multi_timeout(multi_handle, &curl_timeo);
        if (curl_timeo >= 0) {
            timeout.tv_sec = curl_timeo / 1000;
            if (timeout.tv_sec > 1)
                timeout.tv_sec = 1;
            else
                timeout.tv_usec = (curl_timeo % 1000) * 1000;
        }

        /* get file descriptors from the transfers */
        curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

        /* In a real-world program you OF COURSE check the return code of the
         function calls.  On success, the value of maxfd is guaranteed to be
         greater or equal than -1.  We call select(maxfd + 1, ...), specially in
         case of (maxfd == -1), we call select(0, ...), which is basically equal
         to sleep. */

        rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

        switch (rc) {
        case -1:
            /* select error */
            still_running = 0;
            printf("select() returns error, this is badness\n");
            break;
        case 0:
        default:
            /* timeout or readable/writable sockets */
            curl_multi_perform(multi_handle, &still_running);
            break;
        }
    }

    curl_multi_cleanup(multi_handle);

    curl_easy_cleanup(http_handle);

    return 0;
}
