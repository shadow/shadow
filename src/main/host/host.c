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

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "lib/tsc/tsc.h"
#include "main/core/support/config_handlers.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timer.h"
#include "main/host/descriptor/transport.h"
#include "main/host/descriptor/udp.h"
#include "main/host/futex_table.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/host/process.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/utility/utility.h"

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
    Tsc tsc;

    /* the virtual processes this host is running */
    GQueue* processes;

    /* a statistics tracker for in/out bytes, CPU, memory, etc. */
    Tracker* tracker;

    /* virtual process and event id counter */
    guint processIDCounter;
    guint64 eventIDCounter;
    guint64 packetIDCounter;

    /* map abstract socket addresses to unix sockets */
    Arc_AtomicRefCell_AbstractUnixNamespace* abstractUnixNamespace;

    /* map address to futex objects */
    FutexTable* futexTable;

    /* track the order in which the application sent us application data */
    gdouble packetPriorityCounter;

    /* Shared memory allocation for shared state with shim. */
    ShMemBlock shimSharedMemBlock;

    /* Lock protecting parts of shimSharedMemBlock. */
    ShimShmemHostLock* shimShmemHostLock;

    /* random stream */
    Random* random;

#ifdef USE_PERF_TIMERS
    /* track the time spent executing this host */
    GTimer* executionTimer;
#endif

    gchar* dataDirPath;

    gint referenceCount;
    MAGIC_DECLARE;
};

static uint32_t _unblockedSyscallLimit = 0;
ADD_CONFIG_HANDLER(config_getUnblockedSyscallLimit, _unblockedSyscallLimit)

static SimulationTime _unblockedSyscallLatency;
ADD_CONFIG_HANDLER(config_getUnblockedSyscallLatency, _unblockedSyscallLatency)

/* this function is called by manager before the workers exist */
Host* host_new(HostParameters* params) {
    utility_assert(params);

    Host* host = g_new0(Host, 1);
    MAGIC_INIT(host);

#ifdef USE_PERF_TIMERS
    /* start tracking execution time for this host.
     * creating the timer automatically starts it. */
    host->executionTimer = g_timer_new();
#endif

    /* first copy the entire struct of params */
    host->params = *params;

    /* now dup the strings so we own them */
    utility_assert(params->hostname);
    host->params.hostname = g_strdup(params->hostname);
    if(params->pcapDir) host->params.pcapDir = g_strdup(params->pcapDir);

    /* thread-level event communication with other nodes */
    g_mutex_init(&(host->lock));

    host->interfaces = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify) networkinterface_free);

    /* applications this node will run */
    host->processes = g_queue_new();

    info("Created host id '%u' name '%s'", (guint)host->params.id,
         g_quark_to_string(host->params.id));

    host->shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemhost_size());
    shimshmemhost_init(host_getSharedMem(host), host, _unblockedSyscallLimit, _unblockedSyscallLatency);

    host->processIDCounter = 1000;
    host->referenceCount = 1;

#ifdef USE_PERF_TIMERS
    /* we go back to the manager setup process here, so stop counting this host execution */
    g_timer_stop(host->executionTimer);
#endif

    worker_count_allocation(Host);

    return host;
}

/* this function is called by manager before the workers exist */
void host_setup(Host* host, DNS* dns, guint rawCPUFreq, const gchar* hostRootPath) {
    MAGIC_ASSERT(host);

    /* get unique virtual address identifiers for each network interface */
    Address* loopbackAddress =
        dns_register(dns, host->params.id, host->params.hostname, htonl(INADDR_LOOPBACK));
    Address* ethernetAddress =
        dns_register(dns, host->params.id, host->params.hostname, host->params.ipAddr);

    if (loopbackAddress == NULL || ethernetAddress == NULL) {
        /* we should have caught this earlier when we were assigning IP addresses */
        panic("Could not register address");
    }

    host->defaultAddress = ethernetAddress;
    address_ref(host->defaultAddress);

    if (!host->dataDirPath) {
        host->dataDirPath = g_build_filename(hostRootPath, host->params.hostname, NULL);
        g_mkdir_with_parents(host->dataDirPath, 0775);
    }

    host->random = random_new(host->params.nodeSeed);
    host->cpu = cpu_new(host->params.cpuFrequency, (guint64)rawCPUFreq, host->params.cpuThreshold,
                        host->params.cpuPrecision);

    uint64_t tsc_frequency = Tsc_nativeCyclesPerSecond();
    if (!tsc_frequency) {
        tsc_frequency = host->params.cpuFrequency;
        warning("Couldn't find TSC frequency. rdtsc emulation won't scale accurately wrt "
                "simulation time. For most applications this shouldn't matter.");
    }
    host->tsc = Tsc_create(tsc_frequency);

    host->abstractUnixNamespace = abstractunixnamespace_new();

    /* table to track futexes used by processes/threads */
    host->futexTable = futextable_new();

    /* shadow uses values in KiB/s, but the config uses b/s */
    guint64 bwDownKiBps = host->params.requestedBwDownBits / (8 * 1024);
    guint64 bwUpKiBps = host->params.requestedBwUpBits / (8 * 1024);

    char* pcapDir = NULL;
    if (host->params.pcapDir != NULL) {
        if (g_path_is_absolute(host->params.pcapDir)) {
            pcapDir = g_strdup(host->params.pcapDir);
        } else {
            pcapDir = g_build_path("/", host_getDataPath(host), host->params.pcapDir, NULL);
        }
    }

    /* virtual addresses and interfaces for managing network I/O */
    NetworkInterface* loopback = networkinterface_new(
        host, loopbackAddress, G_MAXUINT32, G_MAXUINT32, pcapDir, host->params.pcapCaptureSize,
        host->params.qdisc, host->params.interfaceBufSize);
    NetworkInterface* ethernet = networkinterface_new(
        host, ethernetAddress, bwDownKiBps, bwUpKiBps, pcapDir, host->params.pcapCaptureSize,
        host->params.qdisc, host->params.interfaceBufSize);

    g_free(pcapDir);

    g_hash_table_replace(
        host->interfaces, GUINT_TO_POINTER((guint)address_toNetworkIP(ethernetAddress)), ethernet);
    g_hash_table_replace(
        host->interfaces, GUINT_TO_POINTER((guint)htonl(INADDR_LOOPBACK)), loopback);

    /* the upstream router that will queue packets until we can receive them.
     * this only applies the the ethernet interface, the loopback interface
     * does not receive packets from a router. */
    host->router = router_new(QUEUE_MANAGER_CODEL, ethernet);
    networkinterface_setRouter(ethernet, host->router);

    address_unref(loopbackAddress);
    address_unref(ethernetAddress);

    info("Setup host id '%u' name '%s' with seed %u, ip %s, "
         "%" G_GUINT64_FORMAT " bwUpKiBps, %" G_GUINT64_FORMAT " bwDownKiBps, "
         "%" G_GUINT64_FORMAT " initSockSendBufSize, %" G_GUINT64_FORMAT " initSockRecvBufSize, "
         "%" G_GUINT64_FORMAT " cpuFrequency, %" G_GUINT64_FORMAT " cpuThreshold, "
         "%" G_GUINT64_FORMAT " cpuPrecision",
         (guint)host->params.id, host->params.hostname, host->params.nodeSeed,
         address_toHostIPString(host->defaultAddress), bwUpKiBps, bwDownKiBps,
         host->params.sendBufSize, host->params.recvBufSize, host->params.cpuFrequency,
         host->params.cpuThreshold, host->params.cpuPrecision);
}

static void _host_free(Host* host) {
    MAGIC_ASSERT(host);
    MAGIC_CLEAR(host);
    g_free(host);

    worker_count_deallocation(Host);
}

/* this is needed outside of the free function, because there are parts of the shutdown
 * process that actually hold references to the host. if you just called host_unref instead
 * of this function, then host_free would never actually get called. */
void host_shutdown(Host* host) {
#ifdef USE_PERF_TIMERS
    g_timer_continue(host->executionTimer);
#endif

    debug("shutting down host %s", host->params.hostname);

    if(host->processes) {
        g_queue_free(host->processes);
    }

    if(host->interfaces) {
        g_hash_table_destroy(host->interfaces);
    }

    if(host->router) {
        router_unref(host->router);
    }

    if (host->abstractUnixNamespace) {
        abstractunixnamespace_free(host->abstractUnixNamespace);
    }

    if (host->futexTable) {
        futextable_unref(host->futexTable);
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

    if(host->params.pcapDir) g_free(host->params.pcapDir);

    g_mutex_clear(&(host->lock));

    if(host->dataDirPath) {
        g_free(host->dataDirPath);
    }

#ifdef USE_PERF_TIMERS
    gdouble totalExecutionTime = g_timer_elapsed(host->executionTimer, NULL);
    g_timer_destroy(host->executionTimer);
    info("host '%s' has been shut down, total execution time was %f seconds", host->params.hostname,
         totalExecutionTime);
#else
    info("host '%s' has been shut down", host->params.hostname);
#endif

    if(host->defaultAddress) address_unref(host->defaultAddress);
    if(host->params.hostname) g_free(host->params.hostname);

    utility_assert(host_getSharedMem(host));
    shimshmemhost_destroy(host_getSharedMem(host));
    shmemallocator_globalFree(&host->shimSharedMemBlock);
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

#ifdef USE_PERF_TIMERS
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
#endif

GQuark host_getID(Host* host) {
    MAGIC_ASSERT(host);
    return host->params.id;
}

/* this function is called by worker after the workers exist */
void host_boot(Host* host) {
    MAGIC_ASSERT(host);

    /* must be done after the default IP exists so tracker_heartbeat works */
    if (host->params.heartbeatInterval != SIMTIME_INVALID) {
        host->tracker = tracker_new(host, host->params.heartbeatInterval,
                                    host->params.heartbeatLogLevel, host->params.heartbeatLogInfo);
    }

    /* start refilling the token buckets for all interfaces */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, host->interfaces);

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        NetworkInterface* interface = value;
        networkinterface_startRefillingTokenBuckets(interface, host);
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

void host_addApplication(Host* host, SimulationTime startTime, SimulationTime stopTime,
                         InterposeMethod interposeMethod, const gchar* pluginName,
                         const gchar* pluginPath, gchar** envv, gchar** argv,
                         bool pause_for_debugging) {
    MAGIC_ASSERT(host);
    {
        ShMemBlockSerialized sharedMemBlockSerial =
            shmemallocator_globalBlockSerialize(&host->shimSharedMemBlock);

        char sharedMemBlockBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&sharedMemBlockSerial, sharedMemBlockBuf);

        /* append to the env */
        envv = g_environ_setenv(envv, "SHADOW_SHM_HOST_BLK", sharedMemBlockBuf, TRUE);
    }
    guint processID = host_getNewProcessID(host);
    Process* proc = process_new(host,
                                processID,
                                startTime,
                                stopTime,
                                interposeMethod,
                                host_getName(host),
                                pluginName,
                                pluginPath,
                                envv,
                                argv,
                                pause_for_debugging);
    g_queue_push_tail(host->processes, proc);
}

void host_freeAllApplications(Host* host) {
    MAGIC_ASSERT(host);
    trace("start freeing applications for host '%s'", host->params.hostname);
    while(!g_queue_is_empty(host->processes)) {
        Process* proc = g_queue_pop_head(host->processes);
        process_stop(proc);
        process_unref(proc);
    }
    trace("done freeing application for host '%s'", host->params.hostname);
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

Tsc* host_getTsc(Host* host) {
    MAGIC_ASSERT(host);
    return &host->tsc;
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

void host_associateInterface(Host* host, const CompatSocket* socket, in_addr_t bindAddress) {
    MAGIC_ASSERT(host);

    /* associate the interfaces corresponding to bindAddress with socket */
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

void host_disassociateInterface(Host* host, const CompatSocket* socket) {
    if (socket == NULL) {
        return;
    }

    in_addr_t bindAddress;
    if (!compatsocket_getSocketName(socket, &bindAddress, NULL)) {
        return;
    }

    if (bindAddress == htonl(INADDR_ANY)) {
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

in_port_t _host_incrementPort(in_port_t port, in_port_t port_on_overflow) {
    uint16_t val = ntohs(port);
    val = (val == UINT16_MAX) ? ntohs(port_on_overflow) : val + 1;
    return htons(val);
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
    in_port_t next = _host_incrementPort(start, htons(MIN_RANDOM_PORT));
    while(next != start) {
        /* this will check all interfaces in the case of INADDR_ANY */
        if (host_isInterfaceAvailable(
                host, type, interfaceIP, next, peerIP, peerPort)) {
            return next;
        }
        next = _host_incrementPort(next, htons(MIN_RANDOM_PORT));
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

Arc_AtomicRefCell_AbstractUnixNamespace* host_getAbstractUnixNamespace(Host* host) {
    return host->abstractUnixNamespace;
}

FutexTable* host_getFutexTable(Host* host) { return host->futexTable; }

Process* host_getProcess(Host* host, pid_t virtualPID) {
    MAGIC_ASSERT(host);

    // TODO: once we have a process table, we can do a constant time lookup instead
    GList* current = g_queue_peek_head_link(host->processes);

    while (current != NULL) {
        Process* proc = current->data;
        if (process_getProcessID(proc) == virtualPID) {
            return proc;
        }
        current = current->next;
    }

    return NULL;
}

Thread* host_getThread(Host* host, pid_t virtualTID) {
    MAGIC_ASSERT(host);

    // TODO: once we have a process table, we can do a constant time lookup instead
    GList* current = g_queue_peek_head_link(host->processes);

    while (current != NULL) {
        Process* proc = current->data;
        Thread* thread = process_getThread(proc, virtualTID);
        if (thread) {
            return thread;
        }
        current = current->next;
    }

    return NULL;
}

pid_t host_getNativeTID(Host* host, pid_t virtualPID, pid_t virtualTID) {
    MAGIC_ASSERT(host);

    // TODO: once we have a process table, we can do a constant time lookup instead
    GList* current = g_queue_peek_head_link(host->processes);
    pid_t nativeTID = 0;

    while (current != NULL) {
        Process* proc = current->data;
        nativeTID = process_findNativeTID(proc, virtualPID, virtualTID);

        if (nativeTID > 0) {
            break;
        }

        current = current->next;
    }

    return nativeTID; // 0 if no process/thread has the given virtual PID/TID
}

ShimShmemHost* host_getSharedMem(Host* host) {
    MAGIC_ASSERT(host);
    utility_assert(host->shimSharedMemBlock.p);
    return host->shimSharedMemBlock.p;
}

ShimShmemHostLock* host_getShimShmemLock(Host* host) {
    MAGIC_ASSERT(host);
    return host->shimShmemHostLock;
}

void host_lockShimShmemLock(Host* host) {
    MAGIC_ASSERT(host);
    host->shimShmemHostLock = shimshmemhost_lock(host_getSharedMem(host));
}

void host_unlockShimShmemLock(Host* host) {
    MAGIC_ASSERT(host);
    shimshmemhost_unlock(host_getSharedMem(host), &host->shimShmemHostLock);
}
