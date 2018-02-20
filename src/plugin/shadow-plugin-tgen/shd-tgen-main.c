/*
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <unistd.h>

#include "shd-tgen.h"

/* store a global pointer to the log func, so we can log in any
 * of our tgen modules without a pointer to the tgen struct */
TGenLogFunc tgenLogFunc = NULL;
GLogLevelFlags tgenLogFilterLevel = G_LOG_LEVEL_MESSAGE;

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

static void _tgenmain_log(GLogLevelFlags level, const gchar* fileName, const gint lineNum, const gchar* functionName, const gchar* format, ...) {
    if(level > tgenLogFilterLevel) {
        return;
    }

    va_list vargs;
    va_start(vargs, format);

    gchar* fileStr = fileName ? g_path_get_basename(fileName) : g_strdup("n/a");
    const gchar* functionStr = functionName ? functionName : "n/a";

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s:%i] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _tgenmain_logLevelToString(level), fileStr, lineNum, functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);
    g_free(fileStr);

    va_end(vargs);
}

static void _tgenmain_cleanup(gint status, gpointer arg) {
    if(arg) {
        TGenDriver* tgen = (TGenDriver*) arg;
        tgendriver_unref(tgen);
    }
}

static gint _tgenmain_run(gint argc, gchar *argv[]) {
    tgenLogFunc = _tgenmain_log;
    tgenLogFilterLevel = G_LOG_LEVEL_MESSAGE;

    /* get our hostname for logging */
    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);

    /* default to message level log until we read config */
    tgen_message("Initializing traffic generator on host %s process id %i", hostname, (gint)getpid());

    // TODO embedding a tgen graphml inside the shadow.config.xml file not yet supported
//    if(argv[1] && g_str_has_prefix(argv[1], "<?xml")) {
//        /* argv contains the xml contents of the xml file */
//        gchar* tempPath = _tgendriver_makeTempFile();
//        GError* error = NULL;
//        gboolean success = g_file_set_contents(tempPath, argv[1], -1, &error);
//        if(success) {
//            graph = tgengraph_new(tempPath);
//        } else {
//            tgen_warning("error (%i) while generating temporary xml file: %s", error->code, error->message);
//        }
//        g_unlink(tempPath);
//        g_free(tempPath);
//    } else {
//        /* argv contains the apth of a graphml config file */
//        graph = tgengraph_new(argv[1]);
//    }

    /* argv[0] is program name, argv[1] should be config file */
    if (argc != 2) {
        tgen_warning("USAGE: %s path/to/tgen.xml", argv[0]);
        tgen_critical("cannot continue: incorrect argument list format")
        return -1;
    }

    /* parse the config file */
    TGenGraph* graph = tgengraph_new(argv[1]);
    if (!graph) {
        tgen_critical("cannot continue: traffic generator config file '%s' failed validation", argv[1]);
        return -1;
    }

    /* set log level, which again defaults to message if no level was configured */
    tgenLogFilterLevel = tgenaction_getLogLevel(tgengraph_getStartAction(graph));

    /* create the new state according to user inputs */
    TGenDriver* tgen = tgendriver_new(graph);

    /* driver should have reffed the graph if it needed it */
    tgengraph_unref(graph);

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
