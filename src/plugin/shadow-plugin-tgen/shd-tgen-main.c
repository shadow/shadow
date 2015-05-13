/*
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <unistd.h>

#include <shd-library.h>
#include "shd-tgen.h"

static const gchar* _tgendriver_logLevelToString(GLogLevelFlags logLevel) {
    switch (logLevel) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

static void _tgendriver_logHandler(const gchar *logDomain, GLogLevelFlags logLevel,
        const gchar *message, GLogLevelFlags* userData) {
    GLogLevelFlags filter = *userData;
    if(logLevel <= filter) {
        g_print("%s\n", message);
    }
}

static void _tgendriver_log(ShadowLogLevel level, const gchar* functionName, const gchar* format, ...) {
    va_list vargs;
    va_start(vargs, format);

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _tgendriver_logLevelToString((GLogLevelFlags)level), functionName, format);
    g_logv(G_LOG_DOMAIN, (GLogLevelFlags)level, newformat->str, vargs);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);

    va_end(vargs);
}

gint main(gint argc, gchar *argv[]) {
    GLogLevelFlags filter = G_LOG_LEVEL_MESSAGE;
    g_log_set_default_handler((GLogFunc)_tgendriver_logHandler, &filter);

    /* create the new state according to user inputs */
    TGenDriver* tgen = tgendriver_new(argc, argv, &_tgendriver_log);
    if(!tgen) {
        tgen_critical("Error initializing new TrafficGen instance");
        return -1;
    }

    /* now we need to watch all the epoll descriptors in our main loop */
    gint mainepolld = epoll_create(1);
    if(mainepolld == -1) {
        tgen_critical("Error in main epoll_create");
        close(mainepolld);
        return -1;
    }

    /* register the tgen epoll descriptor so we can watch its events */
    struct epoll_event mainevent;
    mainevent.events = EPOLLIN|EPOLLOUT;
    mainevent.data.fd = tgendriver_getEpollDescriptor(tgen);
    if(!mainevent.data.fd) {
        tgen_critical("Error retrieving tgen epolld");
        close(mainepolld);
        return -1;
    }
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, mainevent.data.fd, &mainevent);

    /* main loop - wait for events on the trafficgen epoll descriptors */
    struct epoll_event tgenevents[100];
    int nReadyFDs;
    tgen_message("entering main loop to watch descriptors");

    while(TRUE) {
        /* wait for some events */
        tgen_debug("waiting for events");
        nReadyFDs = epoll_wait(mainepolld, tgenevents, 100, -1);
        if(nReadyFDs == -1) {
            tgen_critical("error in client epoll_wait");
            return -1;
        }

        /* activate if something is ready */
        tgen_debug("processing event");
        if(nReadyFDs > 0) {
            tgendriver_activate(tgen);
        }

        /* break out if trafficgen is done */
        if(tgendriver_hasEnded(tgen)) {
            break;
        }
    }

    tgen_message("finished main loop, cleaning up");

    /* de-register the tgen epoll descriptor */
    mainevent.data.fd = tgendriver_getEpollDescriptor(tgen);
    if(mainevent.data.fd) {
        epoll_ctl(mainepolld, EPOLL_CTL_DEL, mainevent.data.fd, &mainevent);
    }

    /* cleanup and close */
    close(mainepolld);
    tgendriver_unref(tgen);

    tgen_message("exiting cleanly");

    return 0;
}
