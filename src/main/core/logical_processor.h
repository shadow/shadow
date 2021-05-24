/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/*
 * Represents a pool of logical processors on which `Worker` threads may run.
 */

#include "main/core/worker.h"

/*
 * Logical processor on which a WorkerPool runs Worker threads.
 */
typedef struct _LogicalProcessors LogicalProcessors;

/* A set of `n` logical processors */
LogicalProcessors* lps_new(int n);
void lps_free(LogicalProcessors* lps);

/* Number of logical processors. Thread safe. */
int lps_n(LogicalProcessors* lps);

#ifdef USE_PERF_TIMERS
/* Track idle time. Thread safe. */
double lps_idleTimerElapsed(LogicalProcessors* lps, int lpi);

/* Call to mark the processor idle. Thread safe. */
void lps_idleTimerContinue(LogicalProcessors* lps, int lpi);

/* Call to mark the processor not-idle. Thread safe. */
void lps_idleTimerStop(LogicalProcessors* lps, int lpi);
#else
// define macros that do nothing
#define lps_idleTimerContinue(lps, lpi)
#define lps_idleTimerStop(lps, lpi);
#endif

/* Add a worker to be run on `lpi`. Caller retains ownership of `worker`. Thread
 * safe. */
void lps_readyPush(LogicalProcessors* lps, int lpi, int workerID);

/* Get a worker ID to run on `lpi`. Returns -1 if there are no more workers to
 * run. Thread safe. */
int lps_popWorkerToRunOn(LogicalProcessors* lps, int lpi);

/* Record that the `worker` previously returned by `lp_readyPopFor` has
 * completed its task. Starts idle timer. Thread safe. */
void lps_donePush(LogicalProcessors* lps, int lpi, int workerID);

/* Call after finishing running a task on all workers to mark all workers ready
 * to run again. NOT thread safe. */
void lps_finishTask(LogicalProcessors* lps);

/* Returns the cpu id that should be used with the `affinity_*` module to run a
 * thread on `lpi` */
int lps_cpuId(LogicalProcessors* lps, int lpi);
