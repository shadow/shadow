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
#include "main/host/descriptor/regular_file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timerfd.h"
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
#include "main/utility/utility.h"

struct _HostCInternal {
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

    /* Enables us to sort objects deterministically based on their creation order. */
    guint64 determinismSequenceCounter;

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

static bool _modelUnblockedSyscallLatencyConfig = false;
ADD_CONFIG_HANDLER(config_getModelUnblockedSyscallLatency, _modelUnblockedSyscallLatencyConfig)

static CSimulationTime _unblockedSyscallLatencyConfig;
ADD_CONFIG_HANDLER(config_getUnblockedSyscallLatency, _unblockedSyscallLatencyConfig)

static CSimulationTime _unblockedVdsoLatencyConfig;
ADD_CONFIG_HANDLER(config_getUnblockedVdsoLatency, _unblockedVdsoLatencyConfig)

static CSimulationTime _maxUnappliedCpuLatencyConfig;
ADD_CONFIG_HANDLER(config_getMaxUnappliedCpuLatency, _maxUnappliedCpuLatencyConfig)

/* this function is called by manager before the workers exist */
HostCInternal* hostc_new(const HostParameters* params) {
    utility_debugAssert(params);

    HostCInternal* host = g_new0(HostCInternal, 1);
    MAGIC_INIT(host);

#ifdef USE_PERF_TIMERS
    /* start tracking execution time for this host.
     * creating the timer automatically starts it. */
    host->executionTimer = g_timer_new();
#endif

    /* first copy the entire struct of params */
    host->params = *params;

    /* now dup the strings so we own them */
    utility_debugAssert(params->hostname);
    host->params.hostname = g_strdup(params->hostname);
    if(params->pcapDir) host->params.pcapDir = g_strdup(params->pcapDir);

    host->interfaces = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify) networkinterface_free);

    /* applications this node will run */
    host->processes = g_queue_new();

    info("Created host id '%u' name '%s'", (guint)host->params.id, host->params.hostname);

    host->shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemhost_size());
    shimshmemhost_init(hostc_getSharedMem(host), host->params.id,
                       _modelUnblockedSyscallLatencyConfig, _maxUnappliedCpuLatencyConfig,
                       _unblockedSyscallLatencyConfig, _unblockedVdsoLatencyConfig);

    host->processIDCounter = 1000;
    host->referenceCount = 1;

#ifdef USE_PERF_TIMERS
    /* we go back to the manager setup process here, so stop counting this host execution */
    g_timer_stop(host->executionTimer);
#endif

    worker_count_allocation(HostCInternal);

    return host;
}

uint64_t hostc_get_bw_down_kiBps(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.requestedBwDownBits / (8 * 1024);
}

uint64_t hostc_get_bw_up_kiBps(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.requestedBwUpBits / (8 * 1024);
}

/* this function is called by manager before the workers exist */
void hostc_setup(HostCInternal* host, DNS* dns, gulong rawCPUFreq, const gchar* hostRootPath) {
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
    host->cpu = cpu_new(host->params.cpuFrequency, rawCPUFreq, host->params.cpuThreshold,
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

    char* pcapDir = NULL;
    if (host->params.pcapDir != NULL) {
        if (g_path_is_absolute(host->params.pcapDir)) {
            pcapDir = g_strdup(host->params.pcapDir);
        } else {
            pcapDir = g_build_path("/", hostc_getDataPath(host), host->params.pcapDir, NULL);
        }
    }

    /* virtual addresses and interfaces for managing network I/O */
    NetworkInterface* loopback =
        networkinterface_new(loopbackAddress, pcapDir, host->params.pcapCaptureSize,
                             host->params.qdisc, host->params.interfaceBufSize, false);
    NetworkInterface* ethernet =
        networkinterface_new(ethernetAddress, pcapDir, host->params.pcapCaptureSize,
                             host->params.qdisc, host->params.interfaceBufSize, true);

    g_free(pcapDir);

    g_hash_table_replace(
        host->interfaces, GUINT_TO_POINTER((guint)address_toNetworkIP(ethernetAddress)), ethernet);
    g_hash_table_replace(
        host->interfaces, GUINT_TO_POINTER((guint)htonl(INADDR_LOOPBACK)), loopback);

    /* the upstream router that will queue packets until we can receive them.
     * this only applies the the ethernet interface, the loopback interface
     * does not receive packets from a router. */
    host->router = router_new();

    address_unref(loopbackAddress);
    address_unref(ethernetAddress);

    info("Setup host id '%u' name '%s' with seed %u, ip %s, "
         "%" G_GUINT64_FORMAT " bwUpKiBps, %" G_GUINT64_FORMAT " bwDownKiBps, "
         "%" G_GUINT64_FORMAT " initSockSendBufSize, %" G_GUINT64_FORMAT " initSockRecvBufSize, "
         "%" G_GUINT64_FORMAT " cpuFrequency, %" G_GUINT64_FORMAT " cpuThreshold, "
         "%" G_GUINT64_FORMAT " cpuPrecision",
         (guint)host->params.id, host->params.hostname, host->params.nodeSeed,
         address_toHostIPString(host->defaultAddress), hostc_get_bw_up_kiBps(host),
         hostc_get_bw_down_kiBps(host), host->params.sendBufSize, host->params.recvBufSize,
         host->params.cpuFrequency, host->params.cpuThreshold, host->params.cpuPrecision);
}

static void _hostc_free(HostCInternal* host) {
    MAGIC_ASSERT(host);
    MAGIC_CLEAR(host);
    g_free(host);

    worker_count_deallocation(HostCInternal);
}

/* this is needed outside of the free function, because there are parts of the shutdown
 * process that actually hold references to the host. if you just called hostc_unref instead
 * of this function, then hostc_free would never actually get called. */
void hostc_shutdown(HostCInternal* host) {
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
        router_free(host->router);
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

    if(host->params.pcapDir) g_free((gchar*)host->params.pcapDir);

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
    if(host->params.hostname) g_free((gchar*)host->params.hostname);

    utility_debugAssert(hostc_getSharedMem(host));
    shimshmemhost_destroy(hostc_getSharedMem(host));
    shmemallocator_globalFree(&host->shimSharedMemBlock);
}

void hostc_unref(HostCInternal* host) {
    MAGIC_ASSERT(host);
    (host->referenceCount)--;
    utility_debugAssert(host->referenceCount >= 0);
    if(host->referenceCount == 0) {
        _hostc_free(host);
    }
}

/* resumes the execution timer for this host */
void hostc_continueExecutionTimer(HostCInternal* host) {
#ifdef USE_PERF_TIMERS
    MAGIC_ASSERT(host);
    g_timer_continue(host->executionTimer);
#endif
}

/* stops the execution timer for this host */
void hostc_stopExecutionTimer(HostCInternal* host) {
#ifdef USE_PERF_TIMERS
    MAGIC_ASSERT(host);
    g_timer_stop(host->executionTimer);
#endif
}

HostId hostc_getID(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.id;
}

/* this function is called by worker after the workers exist */
void hostc_boot(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);

    /* must be done after the default IP exists so tracker_heartbeat works */
    if (host->params.heartbeatInterval != SIMTIME_INVALID) {
        host->tracker = tracker_new(rhost, host->params.heartbeatInterval,
                                    host->params.heartbeatLogLevel, host->params.heartbeatLogInfo);
    }

    /* start refilling the token buckets for all interfaces */
    guint64 bwDownKiBps = hostc_get_bw_down_kiBps(host);
    guint64 bwUpKiBps = hostc_get_bw_up_kiBps(host);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, host->interfaces);

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        NetworkInterface* interface = value;
        networkinterface_startRefillingTokenBuckets(interface, rhost, bwDownKiBps, bwUpKiBps);
    }
}

guint hostc_getNewProcessID(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->processIDCounter++;
}

guint64 hostc_getNewEventID(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->eventIDCounter++;
}

guint64 hostc_getNewPacketID(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->packetIDCounter++;
}

void hostc_addApplication(const Host* rhost, CSimulationTime startTime, CSimulationTime stopTime,
                          const gchar* pluginName, const gchar* pluginPath,
                          const gchar* const* envv, const gchar* const* argv,
                          bool pause_for_debugging) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);

    /* get a mutable version of the env list */
    gchar** envv_dup = g_strdupv((gchar**)envv);

    {
        ShMemBlockSerialized sharedMemBlockSerial =
            shmemallocator_globalBlockSerialize(&host->shimSharedMemBlock);

        char sharedMemBlockBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&sharedMemBlockSerial, sharedMemBlockBuf);

        /* append to the env */
        envv_dup = g_environ_setenv(envv_dup, "SHADOW_SHM_HOST_BLK", sharedMemBlockBuf, TRUE);
    }
    guint processID = hostc_getNewProcessID(host);
    Process* proc = process_new(rhost, processID, startTime, stopTime, hostc_getName(host),
                                pluginName, pluginPath, envv_dup, argv, pause_for_debugging);
    g_queue_push_tail(host->processes, proc);

    /* schedule the start and stop events */
    process_schedule(proc, rhost);

    g_strfreev(envv_dup);
}

void hostc_freeAllApplications(HostCInternal* host) {
    MAGIC_ASSERT(host);
    trace("start freeing applications for host '%s'", host->params.hostname);
    while(!g_queue_is_empty(host->processes)) {
        Process* proc = g_queue_pop_head(host->processes);
        process_stop(proc);
        process_unref(proc);
    }
    trace("done freeing application for host '%s'", host->params.hostname);
}

CPU* hostc_getCPU(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->cpu;
}

Tsc* hostc_getTsc(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return &host->tsc;
}

const gchar* hostc_getName(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.hostname;
}

Address* hostc_getDefaultAddress(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->defaultAddress;
}

in_addr_t hostc_getDefaultIP(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return address_toNetworkIP(host->defaultAddress);
}

Random* hostc_getRandom(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->random;
}

gboolean hostc_autotuneReceiveBuffer(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.autotuneRecvBuf;
}

gboolean hostc_autotuneSendBuffer(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.autotuneSendBuf;
}

NetworkInterface* hostc_lookupInterface(HostCInternal* host, in_addr_t handle) {
    MAGIC_ASSERT(host);
    return g_hash_table_lookup(host->interfaces, GUINT_TO_POINTER(handle));
}

Router* hostc_getUpstreamRouter(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->router;
}

void hostc_associateInterface(HostCInternal* host, const CompatSocket* socket,
                              in_addr_t bindAddress) {
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
        NetworkInterface* interface = hostc_lookupInterface(host, bindAddress);
        networkinterface_associate(interface, socket);
    }
}

void hostc_disassociateInterface(HostCInternal* host, const CompatSocket* socket) {
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
        NetworkInterface* interface = hostc_lookupInterface(host, bindAddress);
        networkinterface_disassociate(interface, socket);
    }
}

guint64 hostc_getConfiguredRecvBufSize(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.recvBufSize;
}

guint64 hostc_getConfiguredSendBufSize(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.sendBufSize;
}

gboolean hostc_doesInterfaceExist(HostCInternal* host, in_addr_t interfaceIP) {
    MAGIC_ASSERT(host);

    if(interfaceIP == htonl(INADDR_ANY)) {
        if(g_hash_table_size(host->interfaces) > 0) {
            return TRUE;
        } else {
            return FALSE;
        }
    }

    NetworkInterface* interface = hostc_lookupInterface(host, interfaceIP);
    if(interface) {
        return TRUE;
    }

    return FALSE;
}

gboolean hostc_isInterfaceAvailable(HostCInternal* host, ProtocolType type, in_addr_t interfaceIP,
                                    in_port_t port, in_addr_t peerIP, in_port_t peerPort) {
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
        NetworkInterface* interface = hostc_lookupInterface(host, interfaceIP);
        isAvailable = !networkinterface_isAssociated(interface, type, port, peerIP, peerPort);
    }

    return isAvailable;
}

in_port_t _hostc_incrementPort(in_port_t port, in_port_t port_on_overflow) {
    uint16_t val = ntohs(port);
    val = (val == UINT16_MAX) ? ntohs(port_on_overflow) : val + 1;
    return htons(val);
}

static in_port_t _hostc_getRandomPort(HostCInternal* host) {
    gdouble randomFraction = random_nextDouble(host->random);
    gdouble numPotentialPorts = (gdouble)(UINT16_MAX - MIN_RANDOM_PORT);

    gdouble randomPick = round(randomFraction * numPotentialPorts);
    in_port_t randomHostPort = (in_port_t) randomPick;

    /* make sure we don't assign any low privileged ports */
    randomHostPort += (in_port_t)MIN_RANDOM_PORT;

    utility_debugAssert(randomHostPort >= MIN_RANDOM_PORT);
    return htons(randomHostPort);
}

in_port_t hostc_getRandomFreePort(HostCInternal* host, ProtocolType type, in_addr_t interfaceIP,
                                  in_addr_t peerIP, in_port_t peerPort) {
    MAGIC_ASSERT(host);

    /* we need a random port that is free everywhere we need it to be.
     * we have two modes here: first we just try grabbing a random port until we
     * get a free one. if we cannot find one fast enough, then as a fallback we
     * do an inefficient linear search that is guaranteed to succeed or fail. */

    /* if choosing randomly doesn't succeed within 10 tries, then we have already
     * allocated a lot of ports (>90% on average). then we fall back to linear search. */
    for(guint i = 0; i < 10; i++) {
        in_port_t randomPort = _hostc_getRandomPort(host);

        /* this will check all interfaces in the case of INADDR_ANY */
        if (hostc_isInterfaceAvailable(host, type, interfaceIP, randomPort, peerIP, peerPort)) {
            return randomPort;
        }
    }

    /* now if we tried too many times and still don't have a port, fall back
     * to a linear search to make sure we get a free port if we have one.
     * but start from a random port instead of the min. */
    in_port_t start = _hostc_getRandomPort(host);
    in_port_t next = _hostc_incrementPort(start, htons(MIN_RANDOM_PORT));
    while(next != start) {
        /* this will check all interfaces in the case of INADDR_ANY */
        if (hostc_isInterfaceAvailable(host, type, interfaceIP, next, peerIP, peerPort)) {
            return next;
        }
        next = _hostc_incrementPort(next, htons(MIN_RANDOM_PORT));
    }

    gchar* peerIPStr = address_ipToNewString(peerIP);
    warning("unable to find free ephemeral port for %s peer %s:%"G_GUINT16_FORMAT,
            protocol_toString(type), peerIPStr, (guint16) ntohs((uint16_t) peerPort));
    return 0;
}

Tracker* hostc_getTracker(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->tracker;
}

LogLevel hostc_getLogLevel(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->params.logLevel;
}

gdouble hostc_getNextPacketPriority(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return ++(host->packetPriorityCounter);
}

const gchar* hostc_getDataPath(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->dataDirPath;
}

Arc_AtomicRefCell_AbstractUnixNamespace* hostc_getAbstractUnixNamespace(HostCInternal* host) {
    return host->abstractUnixNamespace;
}

FutexTable* hostc_getFutexTable(HostCInternal* host) { return host->futexTable; }

Process* hostc_getProcess(HostCInternal* host, pid_t virtualPID) {
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

Thread* hostc_getThread(HostCInternal* host, pid_t virtualTID) {
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

pid_t hostc_getNativeTID(HostCInternal* host, pid_t virtualPID, pid_t virtualTID) {
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

ShimShmemHost* hostc_getSharedMem(HostCInternal* host) {
    MAGIC_ASSERT(host);
    utility_debugAssert(host->shimSharedMemBlock.p);
    return host->shimSharedMemBlock.p;
}

ShimShmemHostLock* hostc_getShimShmemLock(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->shimShmemHostLock;
}

void hostc_lockShimShmemLock(HostCInternal* host) {
    MAGIC_ASSERT(host);
    host->shimShmemHostLock = shimshmemhost_lock(hostc_getSharedMem(host));
}

void hostc_unlockShimShmemLock(HostCInternal* host) {
    MAGIC_ASSERT(host);
    shimshmemhost_unlock(hostc_getSharedMem(host), &host->shimShmemHostLock);
}

guint64 hostc_getNextDeterministicSequenceValue(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->determinismSequenceCounter++;
}