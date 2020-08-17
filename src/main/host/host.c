/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timer.h"
#include "main/host/descriptor/transport.h"
#include "main/host/descriptor/udp.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/host/process.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/log_level.h"
#include "support/logger/logger.h"

struct _Host {
    /* general node lock. nothing that belongs to the node should be touched
     * unless holding this lock. everything following this falls under the lock. */
    GMutex lock;

    HostParameters params;

    /* The router upstream from the host, from which we receive packets. */
    Router* router;

    GHashTable* interfaces;
    Address* defaultAddress;
    CPU* cpu;

    /* the virtual processes this host is running */
    GQueue* processes;

    /* a statistics tracker for in/out bytes, CPU, memory, etc. */
    Tracker* tracker;

    /* virtual process and event id counter */
    guint processIDCounter;
    guint64 eventIDCounter;
    guint64 packetIDCounter;

    /* map path to ports for unix sockets */
    GHashTable* unixPathToPortMap;

    /* track the order in which the application sent us application data */
    gdouble packetPriorityCounter;

    /* random stream */
    Random* random;

    /* track the time spent executing this host */
    GTimer* executionTimer;

    gchar* dataDirPath;

    gint referenceCount;
    MAGIC_DECLARE;
};

/* this function is called by slave before the workers exist */
Host* host_new(HostParameters* params) {
    utility_assert(params);

    Host* host = g_new0(Host, 1);
    MAGIC_INIT(host);

    /* start tracking execution time for this host.
     * creating the timer automatically starts it. */
    host->executionTimer = g_timer_new();

    /* first copy the entire struct of params */
    host->params = *params;

    /* now dup the strings so we own them */
    if(params->hostname) host->params.hostname = g_strdup(params->hostname);
    if(params->ipHint) host->params.ipHint = g_strdup(params->ipHint);
    if(params->citycodeHint) host->params.citycodeHint = g_strdup(params->citycodeHint);
    if(params->countrycodeHint) host->params.countrycodeHint = g_strdup(params->countrycodeHint);
    if(params->geocodeHint) host->params.geocodeHint = g_strdup(params->geocodeHint);
    if(params->typeHint) host->params.typeHint = g_strdup(params->typeHint);
    if(params->pcapDir) host->params.pcapDir = g_strdup(params->pcapDir);

    /* thread-level event communication with other nodes */
    g_mutex_init(&(host->lock));

    host->interfaces = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify) networkinterface_free);

    /* TODO: deprecated, used to support UNIX sockets. */
    host->unixPathToPortMap = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* applications this node will run */
    host->processes = g_queue_new();

    message("Created host id '%u' name '%s'", (guint)host->params.id, g_quark_to_string(host->params.id));

    host->processIDCounter = 1000;
    host->referenceCount = 1;

    /* we go back to the slave setup process here, so stop counting this host execution */
    g_timer_stop(host->executionTimer);

    worker_countObject(OBJECT_TYPE_HOST, COUNTER_TYPE_NEW);

    return host;
}

/* this function is called by slave before the workers exist */
void host_setup(Host* host, DNS* dns, Topology* topology, guint rawCPUFreq, const gchar* hostRootPath) {
    MAGIC_ASSERT(host);

    /* get unique virtual address identifiers for each network interface */
    Address* loopbackAddress = dns_register(dns, host->params.id, host->params.hostname, "127.0.0.1");
    Address* ethernetAddress = dns_register(dns, host->params.id, host->params.hostname, host->params.ipHint);
    host->defaultAddress = ethernetAddress;
    address_ref(host->defaultAddress);

    if(!host->dataDirPath) {
        host->dataDirPath = g_build_filename(hostRootPath, host->params.hostname, NULL);
        g_mkdir_with_parents(host->dataDirPath, 0775);
    }

    host->random = random_new(host->params.nodeSeed);
    host->cpu = cpu_new(host->params.cpuFrequency, (guint64)rawCPUFreq, host->params.cpuThreshold, host->params.cpuPrecision);

    /* connect to topology and get the default bandwidth */
    guint64 bwDownKiBps = 0, bwUpKiBps = 0;
    topology_attach(topology, ethernetAddress, host->random,
            host->params.ipHint, host->params.citycodeHint, host->params.countrycodeHint, host->params.geocodeHint,
            host->params.typeHint, &bwDownKiBps, &bwUpKiBps);

    /* prefer assigned bandwidth if available */
    if(host->params.requestedBWDownKiBps) {
        bwDownKiBps = host->params.requestedBWDownKiBps;
    }
    if(host->params.requestedBWUpKiBps) {
        bwUpKiBps = host->params.requestedBWUpKiBps;
    }

    /* virtual addresses and interfaces for managing network I/O */
    NetworkInterface* loopback = networkinterface_new(loopbackAddress, G_MAXUINT32, G_MAXUINT32,
            host->params.logPcap, host->params.pcapDir, host->params.qdisc, host->params.interfaceBufSize);
    NetworkInterface* ethernet = networkinterface_new(ethernetAddress, bwDownKiBps, bwUpKiBps,
            host->params.logPcap, host->params.pcapDir, host->params.qdisc, host->params.interfaceBufSize);

    g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)address_toNetworkIP(ethernetAddress)), ethernet);
    g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)htonl(INADDR_LOOPBACK)), loopback);

    /* the upstream router that will queue packets until we can receive them.
     * this only applies the the ethernet interface, the loopback interface
     * does not receive packets from a router. */
    host->router = router_new(QUEUE_MANAGER_CODEL, ethernet);
    networkinterface_setRouter(ethernet, host->router);

    address_unref(loopbackAddress);
    address_unref(ethernetAddress);

    message("Setup host id '%u' name '%s' with seed %u, ip %s, "
                "%"G_GUINT64_FORMAT" bwUpKiBps, %"G_GUINT64_FORMAT" bwDownKiBps, "
                "%"G_GUINT64_FORMAT" initSockSendBufSize, %"G_GUINT64_FORMAT" initSockRecvBufSize, "
                "%"G_GUINT64_FORMAT" cpuFrequency, %"G_GUINT64_FORMAT" cpuThreshold, "
                "%"G_GUINT64_FORMAT" cpuPrecision",
                (guint)host->params.id, host->params.hostname, host->params.nodeSeed,
                address_toHostIPString(host->defaultAddress),
                bwUpKiBps, bwDownKiBps, host->params.sendBufSize, host->params.recvBufSize,
                host->params.cpuFrequency, host->params.cpuThreshold, host->params.cpuPrecision);
}

static void _host_free(Host* host) {
    MAGIC_ASSERT(host);
    MAGIC_CLEAR(host);
    g_free(host);

    worker_countObject(OBJECT_TYPE_HOST, COUNTER_TYPE_FREE);
}

/* this is needed outside of the free function, because there are parts of the shutdown
 * process that actually hold references to the host. if you just called host_unref instead
 * of this function, then host_free would never actually get called. */
void host_shutdown(Host* host) {
    g_timer_continue(host->executionTimer);

    info("shutting down host %s", host->params.hostname);

    if(host->processes) {
        g_queue_free(host->processes);
    }

    if(host->defaultAddress) {
        topology_detach(worker_getTopology(), host->defaultAddress);
        //address_unref(host->defaultAddress);
    }

    if(host->interfaces) {
        g_hash_table_destroy(host->interfaces);
    }

    if(host->router) {
        router_unref(host->router);
    }

    if(host->unixPathToPortMap) {
        g_hash_table_destroy(host->unixPathToPortMap);
    }

    if(host->cpu) {
        cpu_free(host->cpu);
    }
    if(host->tracker) {
        tracker_free(host->tracker);
    }

    if(host->random) {
        random_free(host->random);
    }

    if(host->params.ipHint) g_free(host->params.ipHint);
    if(host->params.citycodeHint) g_free(host->params.citycodeHint);
    if(host->params.countrycodeHint) g_free(host->params.countrycodeHint);
    if(host->params.geocodeHint) g_free(host->params.geocodeHint);
    if(host->params.typeHint) g_free(host->params.typeHint);
    if(host->params.pcapDir) g_free(host->params.pcapDir);

    g_mutex_clear(&(host->lock));

    if(host->dataDirPath) {
        g_free(host->dataDirPath);
    }

    gdouble totalExecutionTime = g_timer_elapsed(host->executionTimer, NULL);

    message("host '%s' has been shut down, total execution time was %f seconds",
            host->params.hostname, totalExecutionTime);

    if(host->defaultAddress) address_unref(host->defaultAddress);
    if(host->params.hostname) g_free(host->params.hostname);
    g_timer_destroy(host->executionTimer);
}

void host_ref(Host* host) {
    MAGIC_ASSERT(host);
    (host->referenceCount)++;
}

void host_unref(Host* host) {
    MAGIC_ASSERT(host);
    (host->referenceCount)--;
    utility_assert(host->referenceCount >= 0);
    if(host->referenceCount == 0) {
        _host_free(host);
    }
}

void host_lock(Host* host) {
    MAGIC_ASSERT(host);
    g_mutex_lock(&(host->lock));
}

void host_unlock(Host* host) {
    MAGIC_ASSERT(host);
    g_mutex_unlock(&(host->lock));
}

/* resumes the execution timer for this host */
void host_continueExecutionTimer(Host* host) {
    MAGIC_ASSERT(host);
    g_timer_continue(host->executionTimer);
}

/* stops the execution timer for this host */
void host_stopExecutionTimer(Host* host) {
    MAGIC_ASSERT(host);
    g_timer_stop(host->executionTimer);
}

/* returns the fractional number of seconds that have been spent executing this host */
gdouble host_getElapsedExecutionTime(Host* host) {
    MAGIC_ASSERT(host);
    return g_timer_elapsed(host->executionTimer, NULL);
}

GQuark host_getID(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.id;
}

/* this function is called by worker after the workers exist */
void host_boot(Host* host) {
    MAGIC_ASSERT(host);

    /* must be done after the default IP exists so tracker_heartbeat works */
    host->tracker = tracker_new(host->params.heartbeatInterval, host->params.heartbeatLogLevel, host->params.heartbeatLogInfo);

    /* start refilling the token buckets for all interfaces */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, host->interfaces);

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        NetworkInterface* interface = value;
        networkinterface_startRefillingTokenBuckets(interface);
    }

    /* scheduling the starting and stopping of our virtual processes */
    g_queue_foreach(host->processes, (GFunc)process_schedule, NULL);
}

void host_detachAllPlugins(Host* host) {
    MAGIC_ASSERT(host);
    g_queue_foreach(host->processes, process_detachPlugin, NULL);
}

guint host_getNewProcessID(Host* host) {
    MAGIC_ASSERT(host);
    return host->processIDCounter++;
}

guint64 host_getNewEventID(Host* host) {
    MAGIC_ASSERT(host);
    return host->eventIDCounter++;
}

guint64 host_getNewPacketID(Host* host) {
    MAGIC_ASSERT(host);
    return host->packetIDCounter++;
}

void host_addApplication(Host* host, SimulationTime startTime,
                         SimulationTime stopTime, InterposeMethod interposeMethod,
                         const gchar* pluginName, const gchar* pluginPath,
                         const gchar* pluginSymbol, gchar** envv,
                         gchar** argv) {
    MAGIC_ASSERT(host);
    guint processID = host_getNewProcessID(host);
    Process* proc = process_new(host,
                                processID,
                                startTime,
                                stopTime,
                                interposeMethod,
                                host_getName(host),
                                pluginName,
                                pluginPath,
                                pluginSymbol,
                                envv,
                                argv);
    g_queue_push_tail(host->processes, proc);
}

void host_freeAllApplications(Host* host) {
    MAGIC_ASSERT(host);
    debug("start freeing applications for host '%s'", host->params.hostname);
    while(!g_queue_is_empty(host->processes)) {
        Process* proc = g_queue_pop_head(host->processes);
        process_stop(proc);
        process_unref(proc);
    }
    debug("done freeing application for host '%s'", host->params.hostname);
}

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    const Host* na = a;
    const Host* nb = b;
    MAGIC_ASSERT(na);
    MAGIC_ASSERT(nb);
    return na->params.id > nb->params.id ? +1 : na->params.id < nb->params.id ? -1 : 0;
}

gboolean host_isEqual(Host* a, Host* b) {
    if(a == NULL && b == NULL) {
        return TRUE;
    } else if(a == NULL || b == NULL) {
        return FALSE;
    } else {
        return host_compare(a, b, NULL) == 0;
    }
}

CPU* host_getCPU(Host* host) {
    MAGIC_ASSERT(host);
    return host->cpu;
}

gchar* host_getName(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.hostname;
}

Address* host_getDefaultAddress(Host* host) {
    MAGIC_ASSERT(host);
    return host->defaultAddress;
}

in_addr_t host_getDefaultIP(Host* host) {
    MAGIC_ASSERT(host);
    return address_toNetworkIP(host->defaultAddress);
}

Random* host_getRandom(Host* host) {
    MAGIC_ASSERT(host);
    return host->random;
}

gboolean host_autotuneReceiveBuffer(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.autotuneRecvBuf;
}

gboolean host_autotuneSendBuffer(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.autotuneSendBuf;
}

NetworkInterface* host_lookupInterface(Host* host, in_addr_t handle) {
    MAGIC_ASSERT(host);
    return g_hash_table_lookup(host->interfaces, GUINT_TO_POINTER(handle));
}

Router* host_getUpstreamRouter(Host* host, in_addr_t handle) {
    MAGIC_ASSERT(host);
    NetworkInterface* interface = g_hash_table_lookup(host->interfaces, GUINT_TO_POINTER(handle));
    utility_assert(interface != NULL);
    return networkinterface_getRouter(interface);
}

void host_associateInterface(Host* host, Socket* socket, in_addr_t bindAddress,
                             in_port_t bindPort, in_addr_t peerAddress,
                             in_port_t peerPort) {
    MAGIC_ASSERT(host);

    /* connect up socket layer */
    socket_setPeerName(socket, peerAddress, peerPort);
    socket_setSocketName(socket, bindAddress, bindPort);

    /* now associate the interfaces corresponding to bindAddress with socket */
    if(bindAddress == htonl(INADDR_ANY)) {
        /* need to associate all interfaces */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, host->interfaces);

        while(g_hash_table_iter_next(&iter, &key, &value)) {
            NetworkInterface* interface = value;
            networkinterface_associate(interface, socket);
        }
    } else {
        NetworkInterface* interface = host_lookupInterface(host, bindAddress);
        networkinterface_associate(interface, socket);
    }
}

void host_disassociateInterface(Host* host, Socket* socket) {
    if(!socket || !socket_isBound(socket)) {
        return;
    }

    in_addr_t bindAddress;
    socket_getSocketName(socket, &bindAddress, NULL);

    if(bindAddress == htonl(INADDR_ANY)) {
        /* need to dissociate all interfaces */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, host->interfaces);

        while(g_hash_table_iter_next(&iter, &key, &value)) {
            NetworkInterface* interface = value;
            networkinterface_disassociate(interface, socket);
        }

    } else {
        NetworkInterface* interface = host_lookupInterface(host, bindAddress);
        networkinterface_disassociate(interface, socket);
    }
}

guint64 host_getConfiguredRecvBufSize(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.recvBufSize;
}

guint64 host_getConfiguredSendBufSize(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.sendBufSize;
}

gboolean host_doesInterfaceExist(Host* host, in_addr_t interfaceIP) {
    MAGIC_ASSERT(host);

    if(interfaceIP == htonl(INADDR_ANY)) {
        if(g_hash_table_size(host->interfaces) > 0) {
            return TRUE;
        } else {
            return FALSE;
        }
    }

    NetworkInterface* interface = host_lookupInterface(host, interfaceIP);
    if(interface) {
        return TRUE;
    }

    return FALSE;
}

gboolean host_isInterfaceAvailable(Host* host, ProtocolType type,
                                   in_addr_t interfaceIP, in_port_t port,
                                   in_addr_t peerIP, in_port_t peerPort) {
    MAGIC_ASSERT(host);

    gboolean isAvailable = FALSE;

    if(interfaceIP == htonl(INADDR_ANY)) {
        /* need to check that all interfaces are free */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, host->interfaces);

        while(g_hash_table_iter_next(&iter, &key, &value)) {
            NetworkInterface* interface = value;
            isAvailable = !networkinterface_isAssociated(interface, type, port, peerIP, peerPort);

            /* as soon as one is taken, break out to return FALSE */
            if(!isAvailable) {
                break;
            }
        }
    } else {
        NetworkInterface* interface = host_lookupInterface(host, interfaceIP);
        isAvailable = !networkinterface_isAssociated(interface, type, port, peerIP, peerPort);
    }

    return isAvailable;
}

static in_port_t _host_getRandomPort(Host* host) {
    gdouble randomFraction = random_nextDouble(host->random);
    gdouble numPotentialPorts = (gdouble)(UINT16_MAX - MIN_RANDOM_PORT);

    gdouble randomPick = round(randomFraction * numPotentialPorts);
    in_port_t randomHostPort = (in_port_t) randomPick;

    /* make sure we don't assign any low privileged ports */
    randomHostPort += (in_port_t)MIN_RANDOM_PORT;

    utility_assert(randomHostPort >= MIN_RANDOM_PORT);
    return htons(randomHostPort);
}

in_port_t host_getRandomFreePort(Host* host, ProtocolType type,
                                 in_addr_t interfaceIP, in_addr_t peerIP,
                                 in_port_t peerPort) {
    MAGIC_ASSERT(host);

    /* we need a random port that is free everywhere we need it to be.
     * we have two modes here: first we just try grabbing a random port until we
     * get a free one. if we cannot find one fast enough, then as a fallback we
     * do an inefficient linear search that is guaranteed to succeed or fail. */

    /* if choosing randomly doesn't succeed within 10 tries, then we have already
     * allocated a lot of ports (>90% on average). then we fall back to linear search. */
    for(guint i = 0; i < 10; i++) {
        in_port_t randomPort = _host_getRandomPort(host);

        /* this will check all interfaces in the case of INADDR_ANY */
        if (host_isInterfaceAvailable(
                host, type, interfaceIP, randomPort, peerIP, peerPort)) {
            return randomPort;
        }
    }

    /* now if we tried too many times and still don't have a port, fall back
     * to a linear search to make sure we get a free port if we have one.
     * but start from a random port instead of the min. */
    in_port_t start = _host_getRandomPort(host);
    in_port_t next = (start == UINT16_MAX) ? MIN_RANDOM_PORT : start + 1;
    while(next != start) {
        /* this will check all interfaces in the case of INADDR_ANY */
        if (host_isInterfaceAvailable(
                host, type, interfaceIP, next, peerIP, peerPort)) {
            return next;
        }
        next = (next == UINT16_MAX) ? MIN_RANDOM_PORT : next + 1;
    }

    gchar* peerIPStr = address_ipToNewString(peerIP);
    warning("unable to find free ephemeral port for %s peer %s:%"G_GUINT16_FORMAT,
            protocol_toString(type), peerIPStr, (guint16) ntohs((uint16_t) peerPort));
    return 0;
}

Tracker* host_getTracker(Host* host) {
    MAGIC_ASSERT(host);
    return host->tracker;
}

LogLevel host_getLogLevel(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.logLevel;
}

gdouble host_getNextPacketPriority(Host* host) {
    MAGIC_ASSERT(host);
    return ++(host->packetPriorityCounter);
}

const gchar* host_getDataPath(Host* host) {
    MAGIC_ASSERT(host);
    return host->dataDirPath;
}
