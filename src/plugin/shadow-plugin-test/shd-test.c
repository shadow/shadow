/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shd-test.h"

struct _Test {
    ShadowLogFunc logf;
    ShadowCreateCallbackFunc callf;
    GString* basename;
    guint64 quantity;
    guint64 msgload;
    GString* hostname;
    gint listend;
    gint epolld;
    guint64 nmsgs;
    guint magic;
};

void test_free(Test* test) {
    TEST_ASSERT(test);

    test_message("node %s sent %"G_GUINT64_FORMAT" messages", test->hostname->str, test->nmsgs);

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
    const gchar* usage = "basename=STR quantity=INT msg_load=INT";

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

        test_message("successfully parsed options for %s: "
                "basename=%s quantity=%"G_GUINT64_FORMAT" msg_load=%"G_GUINT64_FORMAT,
                &myname[0], test->basename->str, test->quantity, test->msgload);

        return TRUE;
    } else {
        test_critical("invalid argv string for node %s: %s", &myname[0], argv);
        test_message("USAGE: %s", usage);

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
        test_critical("getaddrinfo(): returned %i host '%s' errno %i: %s",
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
            test_info("host '%s' sent '%i' byte%s to host '%s'",
                            test->basename->str, (gint)b, b == 1 ? "" : "s", chosenNodeBuffer->str);
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

Test* test_new(gint argc, gchar* argv[], ShadowLogFunc logf, ShadowCreateCallbackFunc callf) {
    Test* test = g_new0(Test, 1);
    test->magic = TEST_MAGIC;
    test->logf = logf;
    test->callf = callf;

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
