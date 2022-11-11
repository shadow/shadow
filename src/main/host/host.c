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
#include "main/host/process.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

struct _HostCInternal {
    /* The router upstream from the host, from which we receive packets. */
    Router* router;

    Tsc tsc;

    /* the virtual processes this host is running */
    GQueue* processes;

    /* a statistics tracker for in/out bytes, CPU, memory, etc. */
    Tracker* tracker;

    /* map address to futex objects */
    FutexTable* futexTable;

    /* Shared memory allocation for shared state with shim. */
    ShMemBlock shimSharedMemBlock;

    /* Lock protecting parts of shimSharedMemBlock. */
    ShimShmemHostLock* shimShmemHostLock;

#ifdef USE_PERF_TIMERS
    /* track the time spent executing this host */
    GTimer* executionTimer;
#endif

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
HostCInternal* hostc_new(HostId id, const char* hostName) {
    HostCInternal* host = g_new0(HostCInternal, 1);
    MAGIC_INIT(host);

#ifdef USE_PERF_TIMERS
    /* start tracking execution time for this host.
     * creating the timer automatically starts it. */
    host->executionTimer = g_timer_new();
#endif

    /* applications this node will run */
    host->processes = g_queue_new();

    info("Created host id '%u' name '%s'", (guint)id, hostName);

    host->shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemhost_size());
    shimshmemhost_init(hostc_getSharedMem(host), id, _modelUnblockedSyscallLatencyConfig,
                       _maxUnappliedCpuLatencyConfig, _unblockedSyscallLatencyConfig,
                       _unblockedVdsoLatencyConfig);

#ifdef USE_PERF_TIMERS
    /* we go back to the manager setup process here, so stop counting this host execution */
    g_timer_stop(host->executionTimer);
#endif

    worker_count_allocation(HostCInternal);

    return host;
}

/* this function is called by manager before the workers exist */
void hostc_setup(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);

    uint64_t tsc_frequency = Tsc_nativeCyclesPerSecond();
    if (!tsc_frequency) {
        tsc_frequency = host_paramsCpuFrequencyHz(rhost);
        warning("Couldn't find TSC frequency. rdtsc emulation won't scale accurately wrt "
                "simulation time. For most applications this shouldn't matter.");
    }
    host->tsc = Tsc_create(tsc_frequency);

    /* table to track futexes used by processes/threads */
    host->futexTable = futextable_new();

    /* the upstream router that will queue packets until we can receive them.
     * this only applies the the ethernet interface, the loopback interface
     * does not receive packets from a router. */
    host->router = router_new();
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
void hostc_shutdown(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
#ifdef USE_PERF_TIMERS
    g_timer_continue(host->executionTimer);
#endif

    debug("shutting down host %s", host_getName(rhost));

    if(host->processes) {
        g_queue_free(host->processes);
    }

    if(host->router) {
        router_free(host->router);
    }

    if (host->futexTable) {
        futextable_unref(host->futexTable);
    }

    if(host->tracker) {
        tracker_free(host->tracker);
    }

#ifdef USE_PERF_TIMERS
    gdouble totalExecutionTime = g_timer_elapsed(host->executionTimer, NULL);
    g_timer_destroy(host->executionTimer);
    info("host '%s' has been shut down, total execution time was %f seconds", host->params.hostname,
         totalExecutionTime);
#else
    info("host '%s' has been shut down", host_getName(rhost));
#endif

    utility_debugAssert(hostc_getSharedMem(host));
    shimshmemhost_destroy(hostc_getSharedMem(host));
    shmemallocator_globalFree(&host->shimSharedMemBlock);
}

void hostc_unref(HostCInternal* host) {
    MAGIC_ASSERT(host);
    _hostc_free(host);
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

/* this function is called by worker after the workers exist */
void hostc_boot(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);

    /* must be done after the default IP exists so tracker_heartbeat works */
    CSimulationTime heartbeatInterval = host_paramsHeartbeatInterval(rhost);
    if (heartbeatInterval != SIMTIME_INVALID) {
        host->tracker = tracker_new(rhost, heartbeatInterval, host_paramsHeartbeatLogLevel(rhost),
                                    host_paramsHeartbeatLogInfo(rhost));
    }
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
    guint processID = host_getNewProcessID(rhost);
    Process* proc = process_new(rhost, processID, startTime, stopTime, host_getName(rhost),
                                pluginName, pluginPath, envv_dup, argv, pause_for_debugging);
    g_queue_push_tail(host->processes, proc);

    /* schedule the start and stop events */
    process_schedule(proc, rhost);

    g_strfreev(envv_dup);
}

void hostc_freeAllApplications(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);
    trace("start freeing applications for host '%s'", host_getName(rhost));
    while(!g_queue_is_empty(host->processes)) {
        Process* proc = g_queue_pop_head(host->processes);
        process_stop(proc);
        process_unref(proc);
    }
    trace("done freeing application for host '%s'", host_getName(rhost));
}

Tsc* hostc_getTsc(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return &host->tsc;
}

Router* hostc_getUpstreamRouter(HostCInternal* host) {
    MAGIC_ASSERT(host);
    return host->router;
}

in_port_t _hostc_incrementPort(in_port_t port, in_port_t port_on_overflow) {
    uint16_t val = ntohs(port);
    val = (val == UINT16_MAX) ? ntohs(port_on_overflow) : val + 1;
    return htons(val);
}

static in_port_t _hostc_getRandomPort(const Host* rhost) {
    HostCInternal* host = host_internal(rhost);
    gdouble randomFraction = host_rngDouble(rhost);
    gdouble numPotentialPorts = (gdouble)(UINT16_MAX - MIN_RANDOM_PORT);

    gdouble randomPick = round(randomFraction * numPotentialPorts);
    in_port_t randomHostPort = (in_port_t) randomPick;

    /* make sure we don't assign any low privileged ports */
    randomHostPort += (in_port_t)MIN_RANDOM_PORT;

    utility_debugAssert(randomHostPort >= MIN_RANDOM_PORT);
    return htons(randomHostPort);
}

in_port_t hostc_getRandomFreePort(const Host* rhost, ProtocolType type, in_addr_t interfaceIP,
                                  in_addr_t peerIP, in_port_t peerPort) {
    HostCInternal* host = host_internal(rhost);
    MAGIC_ASSERT(host);

    /* we need a random port that is free everywhere we need it to be.
     * we have two modes here: first we just try grabbing a random port until we
     * get a free one. if we cannot find one fast enough, then as a fallback we
     * do an inefficient linear search that is guaranteed to succeed or fail. */

    /* if choosing randomly doesn't succeed within 10 tries, then we have already
     * allocated a lot of ports (>90% on average). then we fall back to linear search. */
    for(guint i = 0; i < 10; i++) {
        in_port_t randomPort = _hostc_getRandomPort(rhost);

        /* this will check all interfaces in the case of INADDR_ANY */
        if (host_isInterfaceAvailable(rhost, type, interfaceIP, randomPort, peerIP, peerPort)) {
            return randomPort;
        }
    }

    /* now if we tried too many times and still don't have a port, fall back
     * to a linear search to make sure we get a free port if we have one.
     * but start from a random port instead of the min. */
    in_port_t start = _hostc_getRandomPort(rhost);
    in_port_t next = _hostc_incrementPort(start, htons(MIN_RANDOM_PORT));
    while(next != start) {
        /* this will check all interfaces in the case of INADDR_ANY */
        if (host_isInterfaceAvailable(rhost, type, interfaceIP, next, peerIP, peerPort)) {
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