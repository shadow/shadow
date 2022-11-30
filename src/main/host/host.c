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
#include "main/host/descriptor/udp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

struct _HostCInternal {
    /* the virtual processes this host is running */
    GQueue* processes;

    MAGIC_DECLARE;
};

/* this function is called by manager before the workers exist */
HostCInternal* hostc_new(HostId id, const char* hostName) {
    HostCInternal* host = g_new0(HostCInternal, 1);
    MAGIC_INIT(host);

    /* applications this node will run */
    host->processes = g_queue_new();

    info("Created host id '%u' name '%s'", (guint)id, hostName);

    worker_count_allocation(HostCInternal);

    return host;
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
    debug("shutting down host %s", host_getName(rhost));

    if(host->processes) {
        g_queue_free(host->processes);
    }
}

void hostc_unref(HostCInternal* host) {
    MAGIC_ASSERT(host);
    _hostc_free(host);
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
        ShMemBlockSerialized sharedMemBlockSerial = host_serializeShmem(rhost);

        char sharedMemBlockBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&sharedMemBlockSerial, sharedMemBlockBuf);

        /* append to the env */
        envv_dup = g_environ_setenv(envv_dup, "SHADOW_SHM_HOST_BLK", sharedMemBlockBuf, TRUE);
    }
    guint processID = host_getNewProcessID(rhost);
    Process* proc =
        process_new(rhost, processID, startTime, stopTime, host_getName(rhost), pluginName,
                    pluginPath, (const gchar* const*)envv_dup, argv, pause_for_debugging);
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
