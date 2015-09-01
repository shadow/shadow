/*
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <unistd.h>

#include "shd-tgen.h"

#define TGEN_LOG_DOMAIN "tgen"

static const gchar* _tgenmain_logLevelToString(GLogLevelFlags logLevel) {
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

static void _tgenmain_logHandler(const gchar *logDomain, GLogLevelFlags logLevel,
        const gchar *message, gpointer userData) {
    GLogLevelFlags filter = (GLogLevelFlags)GPOINTER_TO_INT(userData);
    if(logLevel <= filter) {
        g_print("%s\n", message);
    }
}

static void _tgenmain_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...) {
    va_list vargs;
    va_start(vargs, format);

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _tgenmain_logLevelToString(level), functionName, format);
    g_logv(TGEN_LOG_DOMAIN, level, newformat->str, vargs);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);

    va_end(vargs);
}

static void _tgenmain_cleanup(gint status, gpointer arg) {
    if(arg) {
        TGenDriver* tgen = (TGenDriver*) arg;
        tgendriver_unref(tgen);
    }
    tgen_message("exiting cleanly");
}

static gint _tgenmain_run(gint argc, gchar *argv[]) {
    gpointer filter = GINT_TO_POINTER(G_LOG_LEVEL_MESSAGE);
    g_log_set_handler(TGEN_LOG_DOMAIN, G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
            _tgenmain_logHandler, filter);

    /* create the new state according to user inputs */
    TGenDriver* tgen = tgendriver_new(argc, argv, &_tgenmain_log);
    if(!tgen) {
        tgen_critical("Error initializing new TrafficGen instance");
        return -1;
    } else {
        on_exit(_tgenmain_cleanup, tgen);
    }

    /* all of the tgen descriptors are watched internally */
    gint tgenepolld = tgendriver_getEpollDescriptor(tgen);
    if(tgenepolld < 0) {
        tgen_critical("Error retrieving tgen epolld");
        return -1;
    }

    /* now we need to watch all of the epoll descriptors in our main loop */
    gint mainepolld = epoll_create(1);
    if(mainepolld < 0) {
        tgen_critical("Error in main epoll_create");
        return -1;
    }

    /* register the tgen epoll descriptor so we can watch its events */
    struct epoll_event mainevent;
    memset(&mainevent, 0, sizeof(struct epoll_event));
    mainevent.events = EPOLLIN|EPOLLOUT;
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, tgenepolld, &mainevent);

    /* main loop - wait for events on the trafficgen epoll descriptors */
    tgen_message("entering main loop to watch descriptors");
    while(TRUE) {
        /* clear the event space */
        memset(&mainevent, 0, sizeof(struct epoll_event));

        /* wait for an event on the tgen descriptor */
        tgen_debug("waiting for events");
        gint nReadyFDs = epoll_wait(mainepolld, &mainevent, 1, -1);

        if(nReadyFDs == -1) {
            tgen_critical("error in client epoll_wait");
            return -1;
        }

        /* activate if something is ready */
        if(nReadyFDs > 0) {
            tgen_debug("processing event");
            tgendriver_activate(tgen);
        }

        /* break out if trafficgen is done */
        if(tgendriver_hasEnded(tgen)) {
            break;
        }
    }

    tgen_message("finished main loop, cleaning up");

    /* de-register the tgen epoll descriptor and close */
    epoll_ctl(mainepolld, EPOLL_CTL_DEL, tgenepolld, NULL);
    close(mainepolld);

    tgen_message("returning 0 from main");

    /* _tgenmain_cleanup() should get called via on_exit */
    return 0;
}

gint main(gint argc, gchar *argv[]) {
    return _tgenmain_run(argc, argv);
}
