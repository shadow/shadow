/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/core/logical_processor.h"

#include <glib.h>

#include "main/core/worker.h"
#include "main/host/affinity.h"

typedef struct _LogicalProcessor LogicalProcessor;
struct _LogicalProcessor {
    /* physical cpuId that this LogicalProcessor will run on, for use with
     * affinity_*. Immutable after initialization, so don't need mutex to
     * access. */
    int cpuId;

    /* Worker*'s to run on this LogicalProcessor. */
    GAsyncQueue* readyWorkers;

    /* Worker*'s that have completed the current task on this LogicalProcessor.
     */
    GAsyncQueue* doneWorkers;

#ifdef USE_PERF_TIMERS
    /* Protects members below */
    GMutex mutex;

    /* Total time that this LogicalProcessor has been idle (Not executing a
     * task). */
    GTimer* idleTimer;
#endif

    MAGIC_DECLARE;
};

struct _LogicalProcessors {
    LogicalProcessor* lps;
    size_t n;
    MAGIC_DECLARE;
};

static LogicalProcessor* _idx(LogicalProcessors* lps, int n) {
    MAGIC_ASSERT(lps);
    utility_assert(n >= 0);
    utility_assert(n < lps->n);
    LogicalProcessor* lp = &lps->lps[n];
    MAGIC_ASSERT(lp);
    return lp;
}

LogicalProcessors* lps_new(int n) {
    LogicalProcessors* lps = g_new(LogicalProcessors, 1);
    *lps = (LogicalProcessors){
        .lps = g_new(LogicalProcessor, n),
        .n = n,
    };
    MAGIC_INIT(lps);

    for (int i = 0; i < n; ++i) {
        LogicalProcessor* lp = &lps->lps[i];
        *lp = (LogicalProcessor){
            .cpuId = affinity_getGoodWorkerAffinity(),
            .readyWorkers = g_async_queue_new(),
            .doneWorkers = g_async_queue_new(),
#ifdef USE_PERF_TIMERS
            .idleTimer = g_timer_new(),
#endif
        };
#ifdef USE_PERF_TIMERS
        g_mutex_init(&lp->mutex);
#endif
        MAGIC_INIT(lp);
    }
    return lps;
}

void lps_free(LogicalProcessors* lps) {
    MAGIC_ASSERT(lps);

    for (int i = 0; i < lps->n; ++i) {
        LogicalProcessor* lp = _idx(lps, i);
        MAGIC_ASSERT(lp);
        g_clear_pointer(&lp->readyWorkers, g_async_queue_unref);
        g_clear_pointer(&lp->doneWorkers, g_async_queue_unref);
#ifdef USE_PERF_TIMERS
        g_clear_pointer(&lp->idleTimer, g_timer_destroy);
        g_mutex_clear(&lp->mutex);
#endif
        MAGIC_CLEAR(lp);
    }

    MAGIC_CLEAR(lps);
    g_free(lps);
}

int lps_n(LogicalProcessors* lps) {
    MAGIC_ASSERT(lps);
    return lps->n;
}

#ifdef USE_PERF_TIMERS
void lps_idleTimerContinue(LogicalProcessors* lps, int lpi) {
    LogicalProcessor* lp = _idx(lps, lpi);
    g_mutex_lock(&lp->mutex);
    g_timer_continue(lp->idleTimer);
    g_mutex_unlock(&lp->mutex);
}

void lps_idleTimerStop(LogicalProcessors* lps, int lpi) {
    LogicalProcessor* lp = _idx(lps, lpi);
    g_mutex_lock(&lp->mutex);
    g_timer_stop(lp->idleTimer);
    g_mutex_unlock(&lp->mutex);
}

double lps_idleTimerElapsed(LogicalProcessors* lps, int lpi) {
    LogicalProcessor* lp = _idx(lps, lpi);
    g_mutex_lock(&lp->mutex);
    double rv = g_timer_elapsed(lp->idleTimer, NULL);
    g_mutex_unlock(&lp->mutex);
    return rv;
}
#endif

void lps_readyPush(LogicalProcessors* lps, int lpi, Worker* worker) {
    LogicalProcessor* lp = _idx(lps, lpi);
    g_async_queue_push_front(lp->readyWorkers, worker);
}

Worker* lps_popWorkerToRunOn(LogicalProcessors* lps, int lpi) {
    LogicalProcessor* lp = _idx(lps, lpi);
    for (int i = 0; i < lps_n(lps); ++i) {
        // Start with workers that last ran on `lpi`; if none are available
        // steal from another in round-robin order.
        int fromLpi = (lpi + i) % lps_n(lps);
        LogicalProcessor* fromLp = _idx(lps, fromLpi);
        Worker* worker = g_async_queue_try_pop(fromLp->readyWorkers);
        if (worker) {
            return worker;
        }
    }
    return NULL;
}

void lps_donePush(LogicalProcessors* lps, int lpi, Worker* worker) {
    LogicalProcessor* lp = _idx(lps, lpi);
    // Push to the *front* of the queue so that the last workers to run the
    // current task, which are freshest in cache, are the first ones to run the
    // next task.
    g_async_queue_push_front(lp->doneWorkers, worker);
}

void lps_finishTask(LogicalProcessors* lps) {
    for (int lpi = 0; lpi < lps->n; ++lpi) {
        LogicalProcessor* lp = _idx(lps, lpi);
        utility_assert(g_async_queue_length(lp->readyWorkers) == 0);

        // Swap `ready` and `done` Qs
        GAsyncQueue* tmp = lp->readyWorkers;
        lp->readyWorkers = lp->doneWorkers;
        lp->doneWorkers = tmp;
    }
}

int lps_cpuId(LogicalProcessors* lps, int lpi) {
    LogicalProcessor* lp = _idx(lps, lpi);

    // No synchronization needed since cpus are never mutated after
    // construction.
    return lp->cpuId;
}
