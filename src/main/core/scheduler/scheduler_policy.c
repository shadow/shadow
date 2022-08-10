/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <pthread.h>
#include <string.h>

#include "lib/logger/logger.h"
#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

typedef struct _HostSingleThreadData HostSingleThreadData;
struct _HostSingleThreadData {
    /* used to cache getHosts() result for memory management as needed */
    GQueue* allHosts;
};

struct _SchedulerPolicy {
    GHashTable* threadToThreadDataMap;
    MAGIC_DECLARE;
};

static HostSingleThreadData* _threaddata_new() {
    HostSingleThreadData* tdata = g_new0(HostSingleThreadData, 1);

    tdata->allHosts = g_queue_new();

    return tdata;
}

static void _threaddata_free(HostSingleThreadData* tdata) {
    if(tdata) {
        if(tdata->allHosts) {
            g_queue_free(tdata->allHosts);
        }
        g_free(tdata);
    }
}

/* this must be run synchronously, or the call must be protected by locks */
void schedulerpolicy_addHost(SchedulerPolicy* policy, Host* host, pthread_t assignedThread) {
    MAGIC_ASSERT(policy);
    utility_alwaysAssert(assignedThread != 0);

    /* each thread keeps track of the hosts it needs to run */
    HostSingleThreadData* tdata =
        g_hash_table_lookup(policy->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread));

    if(!tdata) {
        tdata = _threaddata_new();
        g_hash_table_replace(
            policy->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread), tdata);
    }

    g_queue_push_tail(tdata->allHosts, host);
}

GQueue* schedulerpolicy_getAssignedHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSingleThreadData* tdata =
        g_hash_table_lookup(policy->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));

    if(!tdata) {
        return NULL;
    }

    return tdata->allHosts;
}

void schedulerpolicy_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);

    g_hash_table_destroy(policy->threadToThreadDataMap);

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicy_new() {
    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);

    policy->threadToThreadDataMap = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_threaddata_free);

    return policy;
}
