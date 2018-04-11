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
#include <math.h>

#if 1 /* #ifdef DEBUG */
#define PHOLD_MAGIC 0xABBABAAB
#define PHOLD_ASSERT(obj) g_assert(obj && (obj->magic == PHOLD_MAGIC))
#else
#define PHOLD_MAGIC 0
#define PHOLD_ASSERT(obj)
#endif

#define phold_error(...)     _phold_log(G_LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define phold_critical(...)  _phold_log(G_LOG_LEVEL_CRITICAL, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define phold_warning(...)   _phold_log(G_LOG_LEVEL_WARNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define phold_message(...)   _phold_log(G_LOG_LEVEL_MESSAGE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define phold_info(...)      _phold_log(G_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define phold_debug(...)     _phold_log(G_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define PHOLD_LISTEN_PORT 8998

typedef struct _PHold PHold;
struct _PHold {
    GString* mode;
    GString* basename;
    /* mode is generator */
    struct {
        int active;
        guint64 quantity;
        double location;
        double scale;
    } generator;
    /* mode is peer */
    struct {
        int active;
        guint64 load;
        GString* commandBuffer;
        double* weights;
        int num_weights;
    } peer;

    GString* hostname;
    gint listend;
    gint epolld_in;
    guint64 nmsgs;
    guint magic;
};

GLogLevelFlags pholdLogFilterLevel = G_LOG_LEVEL_INFO;

static const gchar* _phold_logLevelToString(GLogLevelFlags logLevel) {
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

/* our test code only relies on a log function, so let's supply that implementation here */
static void _phold_log(GLogLevelFlags level, const gchar* fileName, const gint lineNum, const gchar* functionName, const gchar* format, ...) {
    if(level > pholdLogFilterLevel) {
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
            _phold_logLevelToString(level), fileStr, lineNum, functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);
    g_free(fileStr);

    va_end(vargs);
}

static double _phold_get_uniform_double() {
    // return a uniform value between 0 and 1
    return ((double)random()) / ((double)RAND_MAX);
}

static double _phold_generate_normal_deviate() {
    // Box-Muller method
    double u = _phold_get_uniform_double();
    double v = _phold_get_uniform_double();
    double x = sqrt(-2 * log(u)) * cos(2 * M_PI * v);
    //double y = sqrt(-2 * log(u)) * sin(2 * M_PI * v);
    return x;
}

static double _phold_generate_normal(double location, double scale) {
    // location is mu, mean of normal distribution
    // scale is sigma, sqrt of the variance of normal distribution
    double z = _phold_generate_normal_deviate();
    return location + (scale * z);
}

static double _phold_generate_exponential(double rate) {
    // inverse transform sampling
    double u = _phold_get_uniform_double();
    return -log(u)/rate;
}

static in_addr_t _phold_lookupIP(PHold* phold, const gchar* hostname) {
    PHOLD_ASSERT(phold);

    in_addr_t ip = htonl(INADDR_NONE);

    if(!hostname) {
        return ip;
    }

    struct addrinfo* info = NULL;

    /* this call does the network query */
    gint result = getaddrinfo((gchar*) hostname, NULL, NULL, &info);

    if (result == 0) {
        ip = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
    } else {
        phold_error("getaddrinfo(): returned %i host '%s' errno %i: %s",
                result, hostname, errno, g_strerror(errno));
    }

    freeaddrinfo(info);

    return ip;
}

static gchar* _phold_chooseNode(PHold* phold) {
    PHOLD_ASSERT(phold);
    g_assert(phold->peer.weights);

    double r = _phold_get_uniform_double();

    double cumulative = 0.0;
    for(int i = 0; i < phold->peer.num_weights; i++) {
        cumulative += phold->peer.weights[i];
        if(cumulative >= r) {
            GString* chosenNodeBuffer = g_string_new(NULL);
            g_string_append_printf(chosenNodeBuffer, "%s%"G_GUINT64_FORMAT, phold->basename->str, i+1);
            return g_string_free(chosenNodeBuffer, FALSE);
        }
    }

    return NULL;
}

static int _phold_sendToNode(PHold* phold, char* nodeName, in_port_t port, void* msg, size_t msg_len) {
    int result = 0;
    in_addr_t nodeIP = _phold_lookupIP(phold, nodeName);

    if(nodeIP != htonl(INADDR_NONE)) {
        /* create a new socket */
        gint socketd = socket(AF_INET, (SOCK_DGRAM | SOCK_NONBLOCK), 0);

        /* get node address for this message */
        struct sockaddr_in node;
        memset(&node, 0, sizeof(struct sockaddr_in));
        node.sin_family = AF_INET;
        node.sin_addr.s_addr = nodeIP;
        node.sin_port = port;
        socklen_t len = sizeof(struct sockaddr_in);

        /* send the message to the node */
        ssize_t b = sendto(socketd, msg, msg_len, 0, (struct sockaddr*) (&node), len);
        if(b > 0) {
            phold->nmsgs++;
            phold_info("host '%s' sent %i byte%s to host '%s'",
                            phold->hostname->str, (gint)b, b == 1 ? "" : "s", nodeName);
            result = TRUE;
        } else if(b < 0) {
            phold_warning("sendto(): returned %i host '%s' errno %i: %s",
                            (gint)b, phold->hostname->str, errno, g_strerror(errno));
            result = FALSE;
        }

        /* close socket */
        close(socketd);
    } else {
        phold_warning("could not find address for node '%s', no message was sent", nodeName);
        result = FALSE;
    }

    return result;
}

static void _phold_sendNewMessage(PHold* phold) {
    PHOLD_ASSERT(phold);

    /* pick a node */
    gchar* chosenNodeName = _phold_chooseNode(phold);
    in_port_t port = (in_port_t)htons(PHOLD_LISTEN_PORT);

    gint8 msg = 64;
    _phold_sendToNode(phold, chosenNodeName, port, &msg, 1);

    if(chosenNodeName) {
        g_free(chosenNodeName);
    }
}

static void _phold_bootstrapMessages(PHold* phold) {
    phold_info("sending %lu message to bootstrap", (long unsigned int)phold->peer.load);
    for(guint64 i = 0; i < phold->peer.load; i++) {
        _phold_sendNewMessage(phold);
    }
}

static void _phold_processCommand(PHold* phold) {
    g_assert(phold->peer.commandBuffer->str[phold->peer.commandBuffer->len-1] == ';');
    phold->peer.commandBuffer->str[phold->peer.commandBuffer->len-1] = 0x0;

    phold_info("processing command of len %d, command='%s'",
            phold->peer.commandBuffer->len, phold->peer.commandBuffer->str);

    gchar* command = phold->peer.commandBuffer->str;
    gchar** weights = g_strsplit(command, (const gchar*) ",", -1);

    phold->peer.num_weights = 0;
    for(int i = 0; weights[i] != NULL; i++) {
        phold->peer.num_weights++;
    }

    phold_info("found %d weights in command", phold->peer.num_weights);

    phold->peer.weights = g_new0(double, phold->peer.num_weights);

    for(int i = 0; i < phold->peer.num_weights; i++) {
        phold->peer.weights[i] = g_ascii_strtod(weights[i], NULL);
        phold_info("found weight=%f", phold->peer.weights[i]);
    }

    g_strfreev(weights);

    g_string_free(phold->peer.commandBuffer, TRUE);
    phold->peer.commandBuffer = NULL;
}

static void _phold_startListening(PHold* phold) {
    PHOLD_ASSERT(phold);

    /* create the socket and get a socket descriptor */
    phold->listend = socket(AF_INET, (SOCK_DGRAM | SOCK_NONBLOCK), 0);
    g_assert(phold->listend != -1);

    /* setup the socket address info, client has outgoing connection to server */
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(PHOLD_LISTEN_PORT);

    /* bind the socket to the server port */
    gint result = bind(phold->listend, (struct sockaddr *) &bindAddr, sizeof(bindAddr));
    g_assert(result != -1);

    /* create an epoll so we can wait for IO events */
    phold->epolld_in = epoll_create(1);
    g_assert(phold->epolld_in != -1);

    /* setup the events we will watch for */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = phold->listend;

    /* start watching socket */
    result = epoll_ctl(phold->epolld_in, EPOLL_CTL_ADD, phold->listend, &ev);
    g_assert(result != -1);
}

static void _phold_activate(PHold* phold) {
    PHOLD_ASSERT(phold);

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event epevs[10];
    memset(epevs, 0, 10*sizeof(struct epoll_event));

    /* collect and process all events that are ready */
    gint nfds = epoll_wait(phold->epolld_in, epevs, 10, 0);
    for (gint i = 0; i < nfds; i++) {
        gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
        gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

        gchar buffer[102400];
        memset(buffer, 0, 102400);
        while(TRUE) {
            gssize nBytes = read(phold->listend, &buffer[0], 102400);
            if(nBytes <= 0) {
                break;
            }
            buffer[nBytes] = 0x0;

            if(phold->peer.commandBuffer) {
                g_string_append_printf(phold->peer.commandBuffer, "%s", buffer);

                phold_info("contents of command buffer: '%s'", phold->peer.commandBuffer->str);

                if(g_str_has_suffix(phold->peer.commandBuffer->str, ";")) {
                    _phold_processCommand(phold);
                    sleep(1);
                    _phold_bootstrapMessages(phold);
                }
            } else {
                for(int i = 0; i < nBytes; i++) {
                    _phold_sendNewMessage(phold);
                }
            }
        }
    }
}

static int _phold_runPeer(PHold* phold) {
    g_assert(phold->peer.active);

    phold->peer.commandBuffer = g_string_new("");
    _phold_startListening(phold);

    /* now we need to watch all of the descriptors in our main loop
     * so we know when we can wait on any of them without blocking. */
    int mainepolld = epoll_create(1);
    if (mainepolld == -1) {
        phold_error("Error in main epoll_create");
        close(mainepolld);
        return EXIT_FAILURE;
    }

    /* the one main epoll descriptor that watches all of the sockets,
     * so we now register that descriptor so we can watch for its events */
    struct epoll_event mainevent;
    mainevent.events = EPOLLIN | EPOLLOUT;
    mainevent.data.fd = phold->epolld_in;
    if (!mainevent.data.fd) {
        phold_error("Error retrieving epoll descriptor");
        close(mainepolld);
        return EXIT_FAILURE;
    }
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, mainevent.data.fd, &mainevent);

    /* main loop - wait for events from the descriptors */
    struct epoll_event events[100];
    int nReadyFDs;
    phold_info("entering main loop to watch descriptors");

    while (1) {
        /* wait for some events */
        phold_debug("waiting for events");
        nReadyFDs = epoll_wait(mainepolld, events, 100, -1);
        if (nReadyFDs == -1) {
            phold_error("Error in client epoll_wait");
            return -1;
        }

        /* activate if something is ready */
        phold_debug("processing event");
        if (nReadyFDs > 0) {
            _phold_activate(phold);
        }

        /* should we ever break? */
    }

    phold_info("finished main loop, cleaning up");

    /* de-register the test epoll descriptor */
    mainevent.data.fd = phold->epolld_in;
    if (mainevent.data.fd) {
        epoll_ctl(mainepolld, EPOLL_CTL_DEL, mainevent.data.fd, &mainevent);
    }

    /* cleanup and close */
    close(mainepolld);

    phold_info("peer done running");

    return EXIT_SUCCESS;
}

static GString* _phold_generateWeights(PHold* phold) {
    PHOLD_ASSERT(phold);
    g_assert(phold->generator.active);

    phold_info("generating weights for %d peers", phold->generator.quantity);

    double totalWeight = 0.0;
    double minWeight = INFINITY;

    double weights[phold->generator.quantity];
    memset(weights, 0, sizeof(double)*phold->generator.quantity);

    for(int i = 0; i < phold->generator.quantity; i++) {
        weights[i] = _phold_generate_normal(phold->generator.location, phold->generator.scale);
        totalWeight += weights[i];
        minWeight = MIN(minWeight, weights[i]);
    }

    /* adjust any negative values */
    if(minWeight < 0) {
        double increment = -minWeight;
        for(int i = 0; i < phold->generator.quantity; i++) {
            weights[i] += increment;
            totalWeight += increment;
        }
    }

    /* normalize */
    for(int i = 0; i < phold->generator.quantity; i++) {
        weights[i] /= totalWeight;
    }

    /* generate message string */
    GString* weightsBuffer = g_string_new(NULL);
    for(int i = 0; i < phold->generator.quantity; i++) {
        g_string_append_printf(weightsBuffer, "%f%s",
                weights[i], (i == phold->generator.quantity-1) ? ";" : ",");
    }

    phold_info("finished generating weights; minWeight=%f totalWeight=%f",
            minWeight, totalWeight);

    return weightsBuffer;
}

static void _phold_broadcast(PHold* phold, GString* messageBuffer) {
    PHOLD_ASSERT(phold);
    g_assert(phold->generator.active);

    in_port_t port = (in_port_t)htons(PHOLD_LISTEN_PORT);

    for(int i = 0; i < phold->generator.quantity; i++) {
        GString* nameBuffer = g_string_new(NULL);
        g_string_append_printf(nameBuffer, "%s%d",
                phold->basename->str, i+1);

        int was_success = _phold_sendToNode(phold, nameBuffer->str, port,
                messageBuffer->str, messageBuffer->len);

        if(was_success) {
            phold_info("successfully sent broadcast message to peer %s", nameBuffer->str);
        } else {
            phold_info("failed to send broadcast message to peer %s", nameBuffer->str);
        }

        g_string_free(nameBuffer, TRUE);
    }
}

static int _phold_runGenerator(PHold* phold) {
    g_assert(phold->generator.active);

    GString* weightsBuffer = _phold_generateWeights(phold);
    if(weightsBuffer) {
        phold_info("sending broadcase message '%s' to all peers", weightsBuffer->str);
        _phold_broadcast(phold, weightsBuffer);
        phold_info("finished broadcasting weights to peers");

        g_string_free(weightsBuffer, TRUE);

        phold_info("generator done running");
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

static gboolean _phold_parseOptions(PHold* phold, gint argc, gchar* argv[]) {
    PHOLD_ASSERT(phold);

    /* basename: name of the test nodes in shadow, without the integer suffix
     * quantity: number of test nodes running in the experiment with the same basename as this one
     * msgload: number of messages to generate, each to a random node */
    const gchar* usage = "mode=generator basename=STR quantity=INT location=FLOAT scale=FLOAT | mode=peer basename=STR load=INT";

    /*
     * generator mode:
     *
     * this mode generates the workload distribution for each node in the experiment
     * according to a normal distribution and sends the config to all of the nodes
     * in the simulation.
     *
     * required args:
     *   mode=generate basename=STR quantity=INT location=FLOAT scale=FLOAT
     *
     * peer mode:
     *
     * this mode runs the nodes that actually send messages to each other according
     * to the weights for each node that are generated by the generator.
     *
     * required args:
     *   mode=peer basename=STR load=INT
     */
#define ARGC_GENERATOR 6
#define ARGC_PEER 4

    if(argc <= ARGC_GENERATOR && argv != NULL) {
        for(gint i = 1; i < ARGC_GENERATOR; i++) {
            gchar* token = argv[i];
            gchar** config = g_strsplit(token, (const gchar*) "=", 2);

            if(!g_ascii_strcasecmp(config[0], "mode")) {
                phold->mode = g_string_new(config[1]);
                i = 6;
            }

            g_strfreev(config);
        }
    }

    if(phold->mode == NULL) {
        phold_warning("Unable to find 'mode' option");
        return FALSE;
    }

    gchar myname[128];
    g_assert(gethostname(&myname[0], 128) == 0);

    if(!g_ascii_strcasecmp(phold->mode->str, "generator")) {
        int found_location = 0;
        int found_scale = 0;

        if(argc <= ARGC_GENERATOR && argv != NULL) {
            for(gint i = 1; i < ARGC_GENERATOR; i++) {
                gchar* token = argv[i];
                gchar** config = g_strsplit(token, (const gchar*) "=", 2);

                if(!g_ascii_strcasecmp(config[0], "mode")) {
                    /* valid option, but we don't need it */
                } else if(!g_ascii_strcasecmp(config[0], "basename")) {
                    phold->basename = g_string_new(config[1]);
                } else if(!g_ascii_strcasecmp(config[0], "quantity")) {
                    phold->generator.quantity = g_ascii_strtoull(config[1], NULL, 10);
                } else if(!g_ascii_strcasecmp(config[0], "location")) {
                    phold->generator.location = g_ascii_strtod(config[1], NULL);
                    found_location = 1;
                } else if(!g_ascii_strcasecmp(config[0], "scale")) {
                    phold->generator.scale = g_ascii_strtod(config[1], NULL);
                    found_scale = 1;
                } else {
                    phold_warning("skipping unknown config option %s=%s", config[0], config[1]);
                }

                g_strfreev(config);
            }
        }

        if(phold->basename != NULL && phold->generator.quantity > 0
                && found_location && found_scale) {
            phold->hostname = g_string_new(&myname[0]);
            phold->generator.active = 1;

            phold_info("successfully parsed options for %s: mode=%s "
                    "basename=%s quantity=%"G_GUINT64_FORMAT" location=%f scale=%f",
                    &myname[0], phold->mode->str,
                    phold->basename->str, phold->generator.quantity,
                    phold->generator.location, phold->generator.scale);

            return TRUE;
        } else {
            phold_error("invalid argv string for node %s: %s", &myname[0], argv);
            phold_info("USAGE: %s", usage);

            return FALSE;
        }
    } else {
        int found_load = 0;

        if(argc <= ARGC_PEER && argv != NULL) {
            for(gint i = 1; i < ARGC_PEER; i++) {
                gchar* token = argv[i];
                gchar** config = g_strsplit(token, (const gchar*) "=", 2);

                if(!g_ascii_strcasecmp(config[0], "mode")) {
                    /* valid option, but we don't need it */
                } else if(!g_ascii_strcasecmp(config[0], "basename")) {
                    phold->basename = g_string_new(config[1]);
                } else if(!g_ascii_strcasecmp(config[0], "load")) {
                    phold->peer.load = g_ascii_strtoull(config[1], NULL, 10);
                    found_load = 1;
                } else {
                    phold_warning("skipping unknown config option %s=%s", config[0], config[1]);
                }

                g_strfreev(config);
            }
        }

        if(phold->basename != NULL && found_load) {
            phold->hostname = g_string_new(&myname[0]);
            phold->peer.active = 1;

            phold_info("successfully parsed options for %s: mode=%s "
                    "basename=%s load=%"G_GUINT64_FORMAT,
                    &myname[0], phold->mode->str,
                    phold->basename->str, phold->peer.load);

            return TRUE;
        } else {
            phold_error("invalid argv string for node %s: %s", &myname[0], argv);
            phold_info("USAGE: %s", usage);

            return FALSE;
        }
    }
}

static void _phold_free(PHold* phold) {
    PHOLD_ASSERT(phold);

    phold_info("%s sent %"G_GUINT64_FORMAT" messages", phold->hostname->str, phold->nmsgs);

    if(phold->listend > 0) {
        close(phold->listend);
    }
    if(phold->epolld_in > 0) {
        close(phold->epolld_in);
    }

    if(phold->hostname) {
        g_string_free(phold->hostname, TRUE);
    }
    if(phold->basename) {
        g_string_free(phold->basename, TRUE);
    }
    if(phold->peer.commandBuffer) {
        g_string_free(phold->peer.commandBuffer, TRUE);
    }
    if(phold->peer.weights) {
        g_free(phold->peer.weights);
    }

    phold->magic = 0;
    g_free(phold);
}

static PHold* phold_new(gint argc, gchar* argv[]) {
    PHold* phold = g_new0(PHold, 1);
    phold->magic = PHOLD_MAGIC;

    if(!_phold_parseOptions(phold, argc, argv)) {
        _phold_free(phold);
        return NULL;
    }

    return phold;
}

/* program execution starts here */
int main(int argc, char *argv[]) {
    pholdLogFilterLevel = G_LOG_LEVEL_INFO;

    /* get our hostname for logging */
    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);

    /* default to info level log until we make it configurable */
    phold_info("Initializing phold test on host %s process id %i", hostname, (gint)getpid());

    /* create the new state according to user inputs */
    PHold* phold = phold_new(argc, argv);
    if (!phold) {
        phold_error("Error initializing new instance");
        return -1;
    }

    int result = 0;
    if(phold->generator.active) {
        result = _phold_runGenerator(phold);
    } else if(phold->peer.active) {
        result = _phold_runPeer(phold);
    } else {
        phold_error("neither generator or peer are active");
        result = EXIT_FAILURE;
    }

    if(phold) {
        _phold_free(phold);
    }

    return result;
}
