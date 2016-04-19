/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#if 1 /* #ifdef DEBUG */
#define TEST_MAGIC 0xABBABAAB
#define TEST_ASSERT(obj) g_assert(obj && (obj->magic == TEST_MAGIC))
#else
#define TEST_MAGIC 0
#define TEST_ASSERT(obj)
#endif

#define test_error(...)     _test_log(G_LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define test_critical(...)  _test_log(G_LOG_LEVEL_CRITICAL, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define test_warning(...)   _test_log(G_LOG_LEVEL_WARNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define test_message(...)   _test_log(G_LOG_LEVEL_MESSAGE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define test_info(...)      _test_log(G_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define test_debug(...)     _test_log(G_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define TEST_LISTEN_PORT 8998

typedef struct _Test Test;
struct _Test {
    GString* basename;
    guint64 quantity;
    guint64 msgload;
    GString* hostname;
    gint listend;
    gint epolld;
    guint64 nmsgs;
    guint magic;
};

GString* testLogDomain = NULL;

static const gchar* _test_logLevelToString(GLogLevelFlags logLevel) {
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

static void _test_logHandler(const gchar *logDomain, GLogLevelFlags logLevel,
        const gchar *message, gpointer userData) {
    GLogLevelFlags filter = (GLogLevelFlags)GPOINTER_TO_INT(userData);
    if(logLevel <= filter) {
        g_print("%s\n", message);
    }
}

/* our test code only relies on a log function, so let's supply that implementation here */
static void _test_log(GLogLevelFlags level, const gchar* fileName, const gint lineNum, const gchar* functionName, const gchar* format, ...) {
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
            _test_logLevelToString(level), fileStr, lineNum, functionName, format);
    g_logv(testLogDomain->str, level, newformat->str, vargs);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);
    g_free(fileStr);

    va_end(vargs);
}

void test_free(Test* test) {
    TEST_ASSERT(test);

    test_info("node %s sent %"G_GUINT64_FORMAT" messages", test->hostname->str, test->nmsgs);

    if(test->listend > 0) {
        close(test->listend);
    }
    if(test->epolld > 0) {
        close(test->epolld);
    }

    if(test->hostname) {
        g_string_free(test->hostname, TRUE);
    }
    if(test->basename) {
        g_string_free(test->basename, TRUE);
    }

    test->magic = 0;
    g_free(test);
}

static gboolean _test_parseOptions(Test* test, gint argc, gchar* argv[]) {
    /* basename: name of the test nodes in shadow, without the integer suffix
     * quantity: number of test nodes running in the experiment with the same basename as this one
     * msgload: number of messages to generate, each to a random node */
    const gchar* usage = "basename=STR quantity=INT msgload=INT";

    if(argc == 4 && argv != NULL) {
        for(gint i = 1; i < 4; i++) {
            gchar* token = argv[i];
            gchar** config = g_strsplit(token, (const gchar*) "=", 2);

            if(!g_ascii_strcasecmp(config[0], "basename")) {
                test->basename = g_string_new(config[1]);
            } else if(!g_ascii_strcasecmp(config[0], "quantity")) {
                test->quantity = g_ascii_strtoull(config[1], NULL, 10);
            } else if(!g_ascii_strcasecmp(config[0], "msgload")) {
                test->msgload = g_ascii_strtoull(config[1], NULL, 10);
            } else {
                test_warning("skipping unknown config option %s=%s", config[0], config[1]);
            }

            g_strfreev(config);
        }
    }

    gchar myname[128];
    g_assert(gethostname(&myname[0], 128) == 0);

    if(test->basename != NULL && test->quantity > 0 && test->msgload > 0) {
        test->hostname = g_string_new(&myname[0]);

        test_info("successfully parsed options for %s: "
                "basename=%s quantity=%"G_GUINT64_FORMAT" msg_load=%"G_GUINT64_FORMAT,
                &myname[0], test->basename->str, test->quantity, test->msgload);

        return TRUE;
    } else {
        test_error("invalid argv string for node %s: %s", &myname[0], argv);
        test_info("USAGE: %s", usage);

        return FALSE;
    }
}

static void _test_startListening(Test* test) {
    /* create the socket and get a socket descriptor */
    test->listend = socket(AF_INET, (SOCK_DGRAM | SOCK_NONBLOCK), 0);
    g_assert(test->listend != -1);

    /* setup the socket address info, client has outgoing connection to server */
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(TEST_LISTEN_PORT);

    /* bind the socket to the server port */
    gint result = bind(test->listend, (struct sockaddr *) &bindAddr, sizeof(bindAddr));
    g_assert(result != -1);

    /* create an epoll so we can wait for IO events */
    test->epolld = epoll_create(1);
    g_assert(test->epolld != -1);

    /* setup the events we will watch for */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = test->listend;

    /* start watching out socket */
    result = epoll_ctl(test->epolld, EPOLL_CTL_ADD, test->listend, &ev);
    g_assert(result != -1);
}

static in_addr_t _test_lookupIP(Test* test, const gchar* hostname) {
    in_addr_t ip = htonl(INADDR_NONE);

    struct addrinfo* info = NULL;

    /* this call does the network query */
    gint result = getaddrinfo((gchar*) hostname, NULL, NULL, &info);

    if (result == 0) {
        ip = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
    } else {
        test_error("getaddrinfo(): returned %i host '%s' errno %i: %s",
                result, hostname, errno, g_strerror(errno));
    }

    freeaddrinfo(info);

    return ip;
}

static void _test_sendNewMessage(Test* test) {
    /* pick a random node */
    gdouble r = (gdouble) rand();
    gdouble f = r / ((gdouble)RAND_MAX);
    gint64 index = (guint64)((((gdouble)(test->quantity - 1)) * f) + 1);

    GString* chosenNodeBuffer = g_string_new(NULL);
    g_string_append_printf(chosenNodeBuffer, "%s%"G_GUINT64_FORMAT, test->basename->str, index);

    in_addr_t chosenNodeIP = _test_lookupIP(test, chosenNodeBuffer->str);
    if(chosenNodeIP != htonl(INADDR_NONE)) {
        /* create a new socket */
        gint socketd = socket(AF_INET, (SOCK_DGRAM | SOCK_NONBLOCK), 0);

        /* get node address for this message */
        struct sockaddr_in node;
        memset(&node, 0, sizeof(struct sockaddr_in));
        node.sin_family = AF_INET;
        node.sin_addr.s_addr = chosenNodeIP;
        node.sin_port = htons(TEST_LISTEN_PORT);
        socklen_t len = sizeof(struct sockaddr_in);

        /* send a 1 byte message to that node */
        gint8 msg = 64;
        ssize_t b = sendto(socketd, &msg, 1, 0, (struct sockaddr*) (&node), len);
        if(b > 0) {
            test->nmsgs++;
            test_info("host '%s' sent %i byte%s to host '%s'",
                            test->hostname->str, (gint)b, b == 1 ? "" : "s", chosenNodeBuffer->str);
        } else if(b < 0) {
            test_warning("sendto(): returned %i host '%s' errno %i: %s",
                            (gint)b, test->basename->str, errno, g_strerror(errno));
        }

        /* close socket */
        close(socketd);
    } else {
        test_warning("could not find address for node '%s', no message was sent", chosenNodeBuffer->str);
    }

    g_string_free(chosenNodeBuffer, TRUE);
}

Test* test_new(gint argc, gchar* argv[]) {
    Test* test = g_new0(Test, 1);
    test->magic = TEST_MAGIC;

    if(!_test_parseOptions(test, argc, argv)) {
        test_free(test);
        return NULL;
    }

    _test_startListening(test);

    for(guint64 i = 0; i < test->msgload; i++) {
        _test_sendNewMessage(test);
    }

    return test;
}

void test_activate(Test* test) {
    TEST_ASSERT(test);

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event epevs[10];
    memset(epevs, 0, 10*sizeof(struct epoll_event));

    /* collect and process all events that are ready */
    gint nfds = epoll_wait(test->epolld, epevs, 10, 0);
    for (gint i = 0; i < nfds; i++) {
        gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
        gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

        gchar buffer[102400];
        while(TRUE) {
            gssize nBytes = read(test->listend, &buffer[0], 102400);
            if(nBytes <= 0) {
                break;
            } else {
                for(gsize j = 0; j < (gsize)nBytes; j++) {
                    _test_sendNewMessage(test);
                }
            }
        }
    }
}

/* program execution starts here */
int main(int argc, char *argv[]) {
    /* construct our unique log domain */
    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);
    testLogDomain = g_string_new(NULL);
    g_string_printf(testLogDomain, "%s-test-%i", hostname, (gint)getpid());

    /* default to info level log until we make it configurable */
    gpointer startupFilter = GINT_TO_POINTER(G_LOG_LEVEL_INFO);
    guint startupID = g_log_set_handler(testLogDomain->str, G_LOG_LEVEL_MASK|G_LOG_FLAG_FATAL|G_LOG_FLAG_RECURSION, _test_logHandler, startupFilter);
    test_info("Initializing phold test on host %s using log domain %s", hostname, testLogDomain->str);

    /* create the new state according to user inputs */
    Test* testState = test_new(argc, argv);
    if (!testState) {
        test_error("Error initializing new instance");
        return -1;
    }

    /* now we need to watch all of the descriptors in our main loop
     * so we know when we can wait on any of them without blocking. */
    int mainepolld = epoll_create(1);
    if (mainepolld == -1) {
        test_error("Error in main epoll_create");
        close(mainepolld);
        return -1;
    }

    /* the one main epoll descriptor that watches all of the sockets,
     * so we now register that descriptor so we can watch for its events */
    struct epoll_event mainevent;
    mainevent.events = EPOLLIN | EPOLLOUT;
    mainevent.data.fd = testState->epolld;
    if (!mainevent.data.fd) {
        test_error("Error retrieving epoll descriptor");
        close(mainepolld);
        return -1;
    }
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, mainevent.data.fd, &mainevent);

    /* main loop - wait for events from the descriptors */
    struct epoll_event events[100];
    int nReadyFDs;
    test_info("entering main loop to watch descriptors");

    while (1) {
        /* wait for some events */
        test_debug("waiting for events");
        nReadyFDs = epoll_wait(mainepolld, events, 100, -1);
        if (nReadyFDs == -1) {
            test_error("Error in client epoll_wait");
            return -1;
        }

        /* activate if something is ready */
        test_debug("processing event");
        if (nReadyFDs > 0) {
            test_activate(testState);
        }

        /* should we ever break? */
    }

    test_info("finished main loop, cleaning up");

    /* de-register the test epoll descriptor */
    mainevent.data.fd = testState->epolld;
    if (mainevent.data.fd) {
        epoll_ctl(mainepolld, EPOLL_CTL_DEL, mainevent.data.fd, &mainevent);
    }

    /* cleanup and close */
    close(mainepolld);
    test_free(testState);

    test_info("exiting cleanly");

    return 0;
}
